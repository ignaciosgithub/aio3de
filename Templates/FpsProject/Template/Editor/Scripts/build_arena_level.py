# Copyright (c) Contributors to the Open 3D Engine Project.
# For complete copyright and license terms please see the LICENSE at the root of this distribution.
# SPDX-License-Identifier: Apache-2.0 OR MIT

# When copying a regenerated Arena.prefab into this template, replace <ProjectName>SpinnerComponent
# with ${Name}SpinnerComponent; users regenerating in their own project need no replacement.

import azlmbr.bus as bus
import azlmbr.asset as asset
import azlmbr.components as components
import azlmbr.editor as editor
import azlmbr.entity as entity
import azlmbr.legacy.general as general
import azlmbr.math as math
import json
import os
import time

pending_lua_entity_references = []
pending_lua_entity_arrays = []
script_component_records = []
level_root_entity_id = None
default_parent_entity_id = None


def component(entity_id, name):
    type_ids = editor.EditorComponentAPIBus(
        bus.Broadcast, "FindComponentTypeIdsByEntityType", [name], entity.EntityType().Game
    )
    if not type_ids:
        raise RuntimeError(f"Missing Editor component type: {name}")
    outcome = editor.EditorComponentAPIBus(bus.Broadcast, "AddComponentsOfType", entity_id, type_ids)
    if not outcome.IsSuccess():
        raise RuntimeError(f"Could not add component {name}: {outcome.GetError()}")
    return outcome.GetValue()[0]


def find_property_path(paths, names):
    for name in names:
        suffix = name.lower()
        for path in paths:
            if path.lower() == suffix or path.lower().endswith("|" + suffix):
                return path
    return None


def component_property_path(component_id, names):
    paths = editor.EditorComponentAPIBus(bus.Broadcast, "BuildComponentPropertyList", component_id)
    for name in names:
        suffix = name.lower()
        matching_paths = [
            path
            for path in paths
            if path.lower() == suffix or path.lower().endswith("|" + suffix)
        ]
        if matching_paths:
            return max(matching_paths, key=lambda path: path.count("|"))
    return None


def set_property(component_id, names, value, required=False):
    path = component_property_path(component_id, names)
    if path is None:
        if required:
            raise RuntimeError(f"Could not find component property {names}")
        return False
    outcome = editor.EditorComponentAPIBus(bus.Broadcast, "SetComponentProperty", component_id, path, value)
    if not outcome.IsSuccess() and required:
        raise RuntimeError(f"Could not set component property {path}: {outcome.GetError()}")
    return outcome.IsSuccess()


def wait_for_script_properties(component_id, script_path, property_names):
    property_paths = []
    resolved_paths = {}
    print(f"Waiting for script properties for {script_path}")
    deadline = time.monotonic() + 10.0
    for attempt in range(600):
        property_paths = editor.EditorComponentAPIBus(
            bus.Broadcast, "BuildComponentPropertyList", component_id
        )
        resolved_paths = {
            name: find_property_path(property_paths, [name])
            for name in property_names
        }
        if all(resolved_paths.values()):
            print(f"Loaded {script_path} properties: {property_paths}")
            return resolved_paths
        if attempt and attempt % 60 == 0:
            print(f"Still waiting for {script_path} properties; paths: {property_paths}")
        if time.monotonic() >= deadline:
            break
        general.idle_wait(0.1)

    missing = [name for name, path in resolved_paths.items() if path is None]
    raise RuntimeError(
        f"Timed out waiting for {script_path} properties {missing}; "
        f"component paths: {property_paths}"
    )


def set_lua_property(component_id, property_name, value):
    component_id = component_id["component_id"]
    path = component_property_path(component_id, [property_name])
    if path is None:
        raise RuntimeError(f"Could not find Lua property {property_name}")
    print(f"Setting Lua property {path}")
    outcome = editor.EditorComponentAPIBus(
        bus.Broadcast, "SetComponentProperty", component_id, path, value
    )
    if not outcome.IsSuccess():
        raise RuntimeError(f"Could not set Lua property {path}: {outcome.GetError()}")
    print(f"Set Lua property {path}")


def set_lua_array_property(script_component, property_name, target_entities):
    component_id = script_component["component_id"]
    values = [target_entity["id"] for target_entity in target_entities]
    pending_lua_entity_arrays.append(
        {
            "source_entity": script_component["entity_name"],
            "script_asset": script_component["asset_hint"],
            "property_name": property_name,
            "target_entities": [target_entity["name"] for target_entity in target_entities],
        }
    )
    property_paths = editor.EditorComponentAPIBus(
        bus.Broadcast, "BuildComponentPropertyList", component_id
    )
    path = find_property_path(property_paths, [property_name])
    if path is None:
        raise RuntimeError(f"Could not find Lua array property {property_name}")
    print(f"Setting Lua array property {path}")

    direct_error = None
    try:
        outcome = editor.EditorComponentAPIBus(
            bus.Broadcast, "SetComponentProperty", component_id, path, values
        )
        if outcome.IsSuccess():
            tree_outcome = editor.EditorComponentAPIBus(
                bus.Broadcast, "BuildComponentPropertyTreeEditor", component_id
            )
            if tree_outcome.IsSuccess():
                tree = tree_outcome.GetValue()
                count_outcome = tree.get_container_count(path)
                if count_outcome.IsSuccess() and count_outcome.GetValue() == len(values):
                    print(f"Set Lua array property {path} through SetComponentProperty")
                    return
                direct_error = f"container count did not match {len(values)} values"
            else:
                direct_error = tree_outcome.GetError()
        else:
            direct_error = outcome.GetError()
    except Exception as error:
        direct_error = str(error)

    print(f"SetComponentProperty could not set {path}; using container API: {direct_error}")
    if "tree" not in locals():
        tree_outcome = editor.EditorComponentAPIBus(
            bus.Broadcast, "BuildComponentPropertyTreeEditor", component_id
        )
        if not tree_outcome.IsSuccess():
            raise RuntimeError(
                f"Could not access Lua property container {path}: {tree_outcome.GetError()}"
            )
        tree = tree_outcome.GetValue()
    count_outcome = tree.get_container_count(path)
    if not count_outcome.IsSuccess():
        raise RuntimeError(f"Could not read Lua property container {path}: {count_outcome.GetError()}")

    count = count_outcome.GetValue()
    while count > len(values):
        index = count - 1
        remove_outcome = tree.remove_container_item(path, index)
        if not remove_outcome.IsSuccess():
            raise RuntimeError(
                f"Could not remove item {index} from Lua property {path}: "
                f"{remove_outcome.GetError()}"
            )
        count -= 1
    while count < len(values):
        add_outcome = tree.add_container_item(path, count, values[count])
        if not add_outcome.IsSuccess():
            raise RuntimeError(
                f"Could not add item {count} to Lua property {path}: {add_outcome.GetError()}"
            )
        count += 1
    count_outcome = tree.get_container_count(path)
    if not count_outcome.IsSuccess() or count_outcome.GetValue() != len(values):
        raise RuntimeError(f"Lua property container {path} did not retain all {len(values)} values")
    print(f"Resized Lua array property {path} to {count_outcome.GetValue()} in the Editor property tree")
    for index, value in enumerate(values):
        item_outcome = tree.get_container_item(path, index)
        if not item_outcome.IsSuccess():
            raise RuntimeError(
                f"Could not read item {index} from Lua property {path}: "
                f"{item_outcome.GetError()}"
            )
        item = item_outcome.GetValue()
        try:
            item_description = f"{item} ({type(item).__name__})"
        except Exception as error:
            item_description = f"<unprintable: {error}>"
        print(f"Lua array property {path}[{index}] after container API: {item_description}; target={value}")


def set_lua_entity_reference(script_component, property_name, target_entity):
    component_id = script_component["component_id"]
    property_paths = editor.EditorComponentAPIBus(
        bus.Broadcast, "BuildComponentPropertyList", component_id
    )
    path = find_property_path(property_paths, [property_name])
    if path is None:
        raise RuntimeError(f"Could not find Lua entity-reference property {property_name}")

    pending_lua_entity_references.append(
        {
            "source_entity": script_component["entity_name"],
            "script_asset": script_component["asset_hint"],
            "property_name": property_name,
            "target_entity": target_entity["name"],
        }
    )
    print(
        f"Queued Lua entity reference {script_component['entity_name']}.{property_name} "
        f"through {path} for serialized-value update"
    )


def postprocess_lua_entity_references():
    prefab_path = os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", "..", "Levels", "Arena", "Arena.prefab")
    )
    with open(prefab_path, "r", encoding="utf-8") as prefab_file:
        prefab = json.load(prefab_file)

    container_id = prefab.get("ContainerEntity", {}).get("Id")
    if not container_id:
        raise RuntimeError("Could not find the Arena prefab container entity")

    entities_by_name = {
        item.get("Name"): item
        for item in prefab.get("Entities", {}).values()
        if item.get("Name")
    }
    parent_values_to_update = []
    for entity_name, entity_data in entities_by_name.items():
        for component_data in entity_data.get("Components", {}).values():
            if "TransformComponent" not in component_data.get("$type", ""):
                continue
            if "Parent Entity" not in component_data:
                raise RuntimeError(f"{entity_name} has no serialized Transform parent")
            if not component_data["Parent Entity"]:
                if entity_name not in ("Arena Geometry", "Arena Gameplay"):
                    raise RuntimeError(f"{entity_name} has an empty Transform parent")
                component_data["Parent Entity"] = container_id
                parent_values_to_update.append(entity_name)
            if not component_data["Parent Entity"]:
                raise RuntimeError(f"{entity_name} has an empty Transform parent")

    def find_script_component(entity_name, asset_hint):
        owner = entities_by_name.get(entity_name)
        if owner is None:
            raise RuntimeError(f"Could not find serialized entity {entity_name}")
        matches = [
            component_data
            for component_data in owner.get("Components", {}).values()
            if component_data.get("$type") == "ScriptEditorComponent"
            and component_data.get("ScriptAsset", {}).get("assetHint", "").lower()
            == asset_hint.lower()
        ]
        if len(matches) != 1:
            raise RuntimeError(
                f"Expected one serialized {asset_hint} component on {entity_name}, "
                f"found {len(matches)}"
            )
        return matches[0]

    for record in script_component_records:
        script_component = find_script_component(record["entity_name"], record["asset_hint"])
        serialized_properties = (
            script_component.get("ScriptComponent", {})
            .get("Properties", {})
            .get("Properties", [])
        )
        serialized_names = {prop.get("name") for prop in serialized_properties}
        missing = [name for name in record["property_names"] if name not in serialized_names]
        if missing:
            raise RuntimeError(
                f"Editor did not serialize the full property list for "
                f"{record['entity_name']} / {record['asset_hint']}: {missing}"
            )

    for reference in pending_lua_entity_references:
        target = entities_by_name.get(reference["target_entity"])
        if target is None:
            raise RuntimeError(f"Could not find serialized entity {reference['target_entity']}")
        script_component = find_script_component(
            reference["source_entity"], reference["script_asset"]
        )
        serialized_properties = (
            script_component.get("ScriptComponent", {})
            .get("Properties", {})
            .get("Properties", [])
        )
        property_entry = next(
            (prop for prop in serialized_properties if prop.get("name") == reference["property_name"]),
            None,
        )
        if property_entry is None:
            raise RuntimeError(
                f"Editor did not serialize property {reference['property_name']} on "
                f"{reference['source_entity']}"
            )
        value = property_entry.get("value")
        if (
            not isinstance(value, dict)
            or value.get("$type") not in ("EntityId", "AZ::EntityId")
            or "value" not in value
        ):
            raise RuntimeError(
                f"Serialized property {reference['source_entity']}.{reference['property_name']} "
                f"is not an AZ::EntityId value"
            )
        value["value"] = target["Id"]

    for array in pending_lua_entity_arrays:
        script_component = find_script_component(array["source_entity"], array["script_asset"])
        serialized_properties = (
            script_component.get("ScriptComponent", {})
            .get("Properties", {})
            .get("Properties", [])
        )
        property_entry = next(
            (prop for prop in serialized_properties if prop.get("name") == array["property_name"]),
            None,
        )
        if property_entry is None:
            raise RuntimeError(
                f"Editor did not serialize array property {array['property_name']} on "
                f"{array['source_entity']}"
            )
        serialized_values = property_entry.get("values")
        if not isinstance(serialized_values, list) or not serialized_values:
            raise RuntimeError(
                f"Editor did not serialize a value container for "
                f"{array['source_entity']}.{array['property_name']}"
            )
        element_template = serialized_values[0]
        if (
            not isinstance(element_template, dict)
            or element_template.get("$type") not in ("EntityId", "AZ::EntityId")
            or "value" not in element_template
        ):
            raise RuntimeError(
                f"Editor serialized an unexpected element type for "
                f"{array['source_entity']}.{array['property_name']}"
            )

        updated_values = []
        for target_name in array["target_entities"]:
            target = entities_by_name.get(target_name)
            if target is None:
                raise RuntimeError(f"Could not find serialized entity {target_name}")
            element = dict(element_template)
            element["value"] = target["Id"]
            updated_values.append(element)
        property_entry["values"] = updated_values

    if pending_lua_entity_references or pending_lua_entity_arrays or parent_values_to_update:
        temporary_path = prefab_path + ".tmp"
        with open(temporary_path, "w", encoding="utf-8") as prefab_file:
            json.dump(prefab, prefab_file, indent=4)
            prefab_file.write("\n")
        os.replace(temporary_path, prefab_path)

    print(
        f"Verified complete Editor-serialized properties for {len(script_component_records)} "
        f"Lua components; updated {len(pending_lua_entity_references)} existing entity-reference "
        f"values and the existing value container for {len(pending_lua_entity_arrays)} arrays "
        f"; set existing Transform parent values for {len(parent_values_to_update)} root groups "
        f"in {prefab_path}"
    )


def save_level_and_wait_for_file():
    prefab_path = os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", "..", "Levels", "Arena", "Arena.prefab")
    )
    previous_mtime = os.stat(prefab_path).st_mtime_ns if os.path.exists(prefab_path) else 0
    save_result = general.save_level()
    print(f"general.save_level returned {save_result}")
    if not save_result:
        raise RuntimeError(f"Editor failed to save Arena level at {prefab_path}")
    deadline = time.monotonic() + 10.0
    while time.monotonic() < deadline:
        general.idle_wait(0.1)
        if os.path.exists(prefab_path) and os.stat(prefab_path).st_mtime_ns > previous_mtime:
            return
    raise RuntimeError(f"Timed out waiting for saved Arena prefab at {prefab_path}")


def asset_id(paths):
    for path in paths:
        result = asset.AssetCatalogRequestBus(bus.Broadcast, "GetAssetIdByPath", path, math.Uuid(), False)
        if result.is_valid():
            return result
    return None


def make_entity(name, position, scale=1.0, component_names=(), parent=None):
    parent_id = parent if parent is not None else default_parent_entity_id
    if parent_id is None or not parent_id.IsValid():
        raise RuntimeError(f"Could not determine a valid parent entity for {name}")
    entity_id = editor.ToolsApplicationRequestBus(bus.Broadcast, "CreateNewEntity", parent_id)
    if not entity_id.IsValid():
        raise RuntimeError(f"Could not create entity {name}")
    editor.EditorEntityAPIBus(bus.Event, "SetName", entity_id, name)
    components.TransformBus(
        bus.Event,
        "SetLocalTranslation",
        entity_id,
        math.Vector3(float(position[0]), float(position[1]), float(position[2])),
    )
    components.TransformBus(bus.Event, "SetLocalUniformScale", entity_id, float(scale))
    result = {"id": entity_id, "name": name, "components": []}
    for component_name in component_names:
        result["components"].append(component(entity_id, component_name))
    return result


def set_mesh(entity, model_path, material_paths):
    model_id = asset_id([model_path])
    if model_id is None:
        raise RuntimeError(f"Missing primitive model asset {model_path}")
    material_id = asset_id(material_paths)
    if material_id is None:
        raise RuntimeError(f"Missing primitive material asset {material_paths}")
    material_component = component(entity["id"], "Material")
    entity["components"].append(material_component)
    model_set = False
    material_set = False
    for component_id in entity["components"]:
        model_set = set_property(component_id, ["Model Asset", "ModelAsset"], model_id) or model_set
        material_set = set_property(
            component_id, ["Material Asset", "Material", "Material Override", "Default Material"], material_id
        ) or material_set
        material_set = set_property(
            component_id,
            ["MaterialSlots|Slots|0|MaterialAsset", "Material Slots|Slots|0|Material Asset"],
            material_id,
        ) or material_set
    if not model_set:
        raise RuntimeError(f"Could not set primitive model asset {model_path}")
    if not material_set:
        raise RuntimeError(f"Could not set primitive material asset {material_paths}")


def add_lua(entity, filename, property_names, gem_script=False):
    script = component(entity["id"], "Lua Script")
    path = f"Scripts/ArenaShooter/{filename}" if gem_script else f"Scripts/Fps/{filename}"
    product_path = path.lower().replace(".lua", ".luac")
    asset_paths = [product_path, path, path.lower()]
    if gem_script:
        asset_paths.append(f"Gems/ArenaShooter/Assets/Scripts/ArenaShooter/{filename}")
        asset_paths.append(f"scripts/arenashooter/{filename.lower().replace('.lua', '.luac')}")
    script_id = asset_id(asset_paths)
    if script_id is None:
        raise RuntimeError(f"Could not resolve script asset {path}")
    set_property(script, ["Script"], script_id, required=True)
    resolved_asset_path = asset.AssetCatalogRequestBus(
        bus.Broadcast, "GetAssetPathById", script_id
    )
    print(f"Resolved {path} to asset product {resolved_asset_path}")
    if not editor.EditorComponentAPIBus(bus.Broadcast, "DisableComponents", [script]):
        raise RuntimeError(f"Could not disable Lua Script component for {path}")
    if not editor.EditorComponentAPIBus(bus.Broadcast, "EnableComponents", [script]):
        raise RuntimeError(f"Could not re-enable Lua Script component for {path}")
    wait_for_script_properties(script, path, property_names)
    script_component = {
        "component_id": script,
        "entity_name": entity["name"],
        "asset_hint": product_path,
        "property_names": property_names,
    }
    script_component_records.append(script_component)
    return script_component


def main():
    global default_parent_entity_id, level_root_entity_id
    general.idle_enable(True)
    print("Starting Arena generation")
    create_result = general.create_level_no_prompt("DefaultLevel", "Arena", 1024, 1, 4096, False)
    print(f"create_level_no_prompt returned {create_result}")
    open_result = general.open_level_no_prompt("Arena")
    if not open_result:
        raise RuntimeError("Could not open Arena")
    level_root_entity_id = editor.ToolsApplicationRequestBus(
        bus.Broadcast, "GetCurrentLevelEntityId"
    )
    if not level_root_entity_id.IsValid():
        raise RuntimeError("Could not get the Arena level root entity")
    geometry_parent = make_entity("Arena Geometry", (0.0, 0.0, 0.0), parent=level_root_entity_id)
    gameplay_parent = make_entity("Arena Gameplay", (0.0, 0.0, 0.0), parent=level_root_entity_id)
    default_parent_entity_id = geometry_parent["id"]

    ground = make_entity(
        "Arena Ground", (0.0, 0.0, -30.0), 60.0,
        ["Mesh", "PhysX Static Rigid Body", "PhysX Primitive Collider"],
    )
    box_material = [
        "objects/_primitives/_box_1x1_middlegrey.azmaterial",
        "objects/_primitives/_box_1x1_middlegrey.material",
    ]
    set_mesh(ground, "objects/_primitives/_box_1x1.fbx.azmodel", box_material)

    wall_index = 0
    for side, positions in [
        ("North", [(x, 27.5, 2.5) for x in range(-27, 30, 5)]),
        ("South", [(x, -27.5, 2.5) for x in range(-27, 30, 5)]),
        ("East", [(27.5, y, 2.5) for y in range(-27, 30, 5)]),
        ("West", [(-27.5, y, 2.5) for y in range(-27, 30, 5)]),
    ]:
        for position in positions:
            wall_index += 1
            wall = make_entity(
                f"{side} Wall {wall_index}", position, 5.0,
                ["Mesh", "PhysX Static Rigid Body", "PhysX Primitive Collider"],
            )
            set_mesh(wall, "objects/_primitives/_box_1x1.fbx.azmodel", box_material)

    for name, position, scale in [
        ("Cover 01", (-18.0, -10.0, 1.5), 3.0),
        ("Cover 02", (-8.0, -16.0, 1.5), 3.0),
        ("Cover 03", (8.0, -12.0, 1.5), 3.0),
        ("Cover 04", (19.0, -8.0, 1.5), 3.0),
        ("Cover 05", (-18.0, 7.0, 1.5), 3.0),
        ("Cover 06", (-8.0, 15.0, 1.5), 3.0),
        ("Cover 07", (8.0, 10.0, 1.5), 3.0),
        ("Cover 08", (18.0, 16.0, 1.5), 3.0),
        ("Cover 09", (-2.0, -18.0, 1.5), 3.0),
        ("Cover 10", (2.0, 18.0, 1.5), 3.0),
        ("Cover 11", (-22.0, 0.0, 1.5), 3.0),
        ("Cover 12", (22.0, 0.0, 1.5), 3.0),
        ("Ramp 01", (-3.0, -4.0, 1.25), 2.5),
        ("Ramp 02", (3.0, 4.0, 1.25), 2.5),
    ]:
        block = make_entity(
            name, position, scale, ["Mesh", "PhysX Static Rigid Body", "PhysX Primitive Collider"]
        )
        if name.startswith("Ramp"):
            components.TransformBus(
                bus.Event,
                "SetLocalRotation",
                block["id"],
                math.Vector3(0.0, 0.2094395, 0.0),
            )
        set_mesh(block, "objects/_primitives/_box_1x1.fbx.azmodel", box_material)

    default_parent_entity_id = gameplay_parent["id"]
    sun = make_entity("Sun", (0.0, 0.0, 0.0), component_names=["Directional Light"])
    components.TransformBus(
        bus.Event,
        "SetLocalRotation",
        sun["id"],
        math.Vector3(-0.8726646, 0.0, 0.5235988),
    )
    set_property(sun["components"][0], ["Enable Shadow"], True, required=True)

    skybox_asset = asset_id(["lightingpresets/highcontrast/goegap_4k_skyboxcm.exr.streamingimage"])
    diffuse_ibl_asset = asset_id(
        ["lightingpresets/highcontrast/goegap_4k_iblglobalcm_ibldiffuse.exr.streamingimage"]
    )
    specular_ibl_asset = asset_id(
        ["lightingpresets/highcontrast/goegap_4k_iblglobalcm_iblspecular.exr.streamingimage"]
    )
    if skybox_asset is None or diffuse_ibl_asset is None or specular_ibl_asset is None:
        raise RuntimeError("Could not resolve DefaultProject Arena lighting cubemaps")
    sky = make_entity(
        "Sky", (0.0, 0.0, 0.0), component_names=["HDRi Skybox", "Global Skylight (IBL)"]
    )
    set_property(sky["components"][0], ["Cubemap Texture", "CubemapAsset"], skybox_asset, required=True)
    set_property(sky["components"][1], ["Diffuse Image", "diffuseImageAsset"], diffuse_ibl_asset, required=True)
    set_property(sky["components"][1], ["Specular Image", "specularImageAsset"], specular_ibl_asset, required=True)

    player = make_entity(
        "Player", (0.0, -22.0, 1.0), component_names=[
            "PhysX Character Controller", "PhysX Character Gameplay", "Input",
        ],
    )
    set_property(player["components"][0], ["Height"], 1.8, required=True)
    input_asset = asset_id(["Gems/ArenaShooter/Assets/InputBindings/arenashooter.inputbindings",
                            "InputBindings/arenashooter.inputbindings"])
    if input_asset is None:
        raise RuntimeError("Could not resolve ArenaShooter input bindings")
    set_property(player["components"][2], ["Input to event bindings"], input_asset, required=True)
    camera = make_entity("Player Camera", (0.0, 0.0, 1.6), parent=player["id"], component_names=["Camera"])
    player_controller = add_lua(
        player,
        "PlayerController.lua",
        ["MoveSpeed", "LookSensitivity", "JumpSpeed", "MaxPitch", "DeviceDetectorEntity", "CameraEntity"],
        True,
    )
    weapon_a = add_lua(
        player,
        "Weapon.lua",
        ["CameraEntity", "Range", "Damage", "ImpactImpulse", "FireInterval", "Slot", "FireSoundEntity"],
        True,
    )
    weapon_switcher = add_lua(
        player, "WeaponSwitcher.lua", ["WeaponCount", "SwitchSoundEntity"], True
    )
    add_lua(player, "Health.lua", ["MaxHealth", "SpawnPoint", "RespawnDelay"], True)
    set_lua_property(weapon_a, "Slot", 1.0)
    set_lua_property(weapon_switcher, "WeaponCount", 1.0)
    set_lua_entity_reference(player_controller, "CameraEntity", camera)
    set_lua_entity_reference(weapon_a, "CameraEntity", camera)

    enemies = []
    for index, position in enumerate([(-20.0, 18.0, 1.0), (20.0, 18.0, 1.0),
                                      (-20.0, -4.0, 1.0), (20.0, -4.0, 1.0)], 1):
        enemy = make_entity(
            f"Enemy {index}", position, 1.8,
            component_names=["PhysX Character Controller", "Mesh"],
        )
        set_mesh(enemy, "objects/_primitives/_box_1x1.fbx.azmodel", box_material)
        behavior_tree = component(enemy["id"], "Behavior Tree")
        set_property(
            behavior_tree, ["Tree file", "TreeFile"], "Scripts/Fps/Enemy.behaviortree.json", required=True
        )
        enemy_brain = add_lua(
            enemy,
            "EnemyBrain.lua",
            ["Player", "SightRange", "AttackRange", "FireInterval", "Damage", "EyeHeight", "PatrolA", "PatrolB"],
        )
        set_lua_entity_reference(enemy_brain, "Player", player)
        add_lua(enemy, "Health.lua", ["MaxHealth", "SpawnPoint", "RespawnDelay"], True)
        enemies.append(enemy)

    manager = make_entity("Game Manager", (0.0, 0.0, 0.0), component_names=["Input"])
    manager_input = asset_id(["Gems/ArenaShooter/Assets/InputBindings/arenashooter.inputbindings",
                              "InputBindings/arenashooter.inputbindings"])
    if manager_input is None:
        raise RuntimeError("Could not resolve ArenaShooter input bindings for Game Manager")
    set_property(manager["components"][0], ["Input to event bindings"], manager_input, required=True)
    score = add_lua(manager, "ScoreHud.lua", ["Targets", "MatchTime"], True)
    add_lua(manager, "DeviceDetector.lua", ["ShowOnScreen"], True)
    settings_menu = add_lua(manager, "SettingsMenu.lua", ["CameraEntity"], True)
    set_lua_array_property(score, "Targets", enemies)
    set_lua_property(score, "MatchTime", 120.0)
    set_lua_entity_reference(settings_menu, "CameraEntity", camera)
    set_lua_entity_reference(player_controller, "DeviceDetectorEntity", manager)

    volume = make_entity("Voxel Nav Volume", (0.0, 0.0, 5.0), component_names=["Voxel Nav Volume"])
    for component_id in volume["components"]:
        set_property(
            component_id, ["Size", "Settings|Size", "Settings|m_size"],
            math.Vector3(60.0, 60.0, 20.0), required=True
        )
        set_property(
            component_id, ["Voxel Size", "Settings|Voxel size", "Settings|m_voxelSize"],
            0.5, required=True
        )

    for index, position in enumerate([(-12.0, -4.0, 2.0), (0.0, 2.0, 2.0),
                                      (12.0, -4.0, 2.0), (0.0, 16.0, 2.0)], 1):
        pickup = make_entity(
            f"Spinner Pickup {index}", position, 1.5, ["Mesh", "${Name} Spinner"],
        )
        sphere_material = [
            "objects/_primitives/_sphere_1x1_middlegrey.azmaterial",
            "objects/_primitives/_sphere_1x1_middlegrey.material",
        ]
        set_mesh(pickup, "objects/_primitives/_sphere_1x1.fbx.azmodel", sphere_material)

    save_level_and_wait_for_file()
    postprocess_lua_entity_references()
    print(f"Generated FPS arena with {len(enemies)} BehaviorTree enemies")


try:
    main()
except Exception as error:
    print(f"Arena generation failed: {error}")
    raise
finally:
    general.exit_no_prompt()
