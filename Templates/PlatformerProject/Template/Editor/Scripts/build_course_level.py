# Copyright (c) Contributors to the Open 3D Engine Project.
# For complete copyright and license terms please see the LICENSE at the root of this distribution.
# SPDX-License-Identifier: Apache-2.0 OR MIT

# When copying a regenerated Course.prefab into this template, replace <ProjectName>SpinnerComponent
# with ${Name}SpinnerComponent; users regenerating in their own project need no replacement.

import azlmbr.asset as asset
import azlmbr.bus as bus
import azlmbr.components as components
import azlmbr.editor as editor
import azlmbr.entity as entity
import azlmbr.legacy.general as general
import azlmbr.math as math
import json
import os
import time

level_root_entity_id = None
parent_records = []
entity_names_by_id = {}
script_records = []
pending_references = []
pending_vector_properties = []
staging_entity_names = ("Always Loaded Staging", "Streamed Staging")
always_loaded_parent_id = None
streamed_parent_id = None


def component(entity_id, name, entity_type=None):
    if entity_type is None:
        entity_type = entity.EntityType().Game
    type_ids = editor.EditorComponentAPIBus(
        bus.Broadcast, "FindComponentTypeIdsByEntityType", [name], entity_type
    )
    if not type_ids:
        raise RuntimeError(f"Missing Editor component type: {name}")
    outcome = editor.EditorComponentAPIBus(bus.Broadcast, "AddComponentsOfType", entity_id, type_ids)
    if not outcome.IsSuccess():
        raise RuntimeError(f"Could not add component {name}: {outcome.GetError()}")
    return outcome.GetValue()[0]


def property_path(component_id, names):
    paths = editor.EditorComponentAPIBus(bus.Broadcast, "BuildComponentPropertyList", component_id)
    for name in names:
        suffix = name.lower()
        matches = [path for path in paths if path.lower() == suffix or path.lower().endswith("|" + suffix)]
        if matches:
            return max(matches, key=lambda path: path.count("|"))
    return None


def set_property(component_id, names, value, required=False):
    path = property_path(component_id, names)
    if path is None:
        if required:
            raise RuntimeError(f"Could not find component property {names}")
        return False
    outcome = editor.EditorComponentAPIBus(
        bus.Broadcast, "SetComponentProperty", component_id, path, value
    )
    if not outcome.IsSuccess() and required:
        raise RuntimeError(f"Could not set component property {path}: {outcome.GetError()}")
    return outcome.IsSuccess()


def set_lua_property(script, name, value):
    set_property(script["component"], [name], value, required=True)


def set_lua_vector_property(script, name, value):
    pending_vector_properties.append(
        (script["entity"], script["asset_hint"], name, tuple(value))
    )


def wait_for_script(script_component, path, names):
    deadline = time.monotonic() + 10.0
    last_paths = []
    while time.monotonic() < deadline:
        last_paths = editor.EditorComponentAPIBus(
            bus.Broadcast, "BuildComponentPropertyList", script_component
        )
        if all(any(item.lower() == name.lower() or item.lower().endswith("|" + name.lower())
                   for item in last_paths) for name in names):
            return
        general.idle_wait(0.1)
    raise RuntimeError(f"Timed out waiting for {path} properties {names}; got {last_paths}")


def asset_id(paths):
    for path in paths:
        result = asset.AssetCatalogRequestBus(
            bus.Broadcast, "GetAssetIdByPath", path, math.Uuid(), False
        )
        if result.is_valid():
            return result
    return None


def make_entity(name, position=(0.0, 0.0, 0.0), parent=None, components_to_add=()):
    parent_id = parent if parent is not None else level_root_entity_id
    entity_id = editor.ToolsApplicationRequestBus(bus.Broadcast, "CreateNewEntity", parent_id)
    if not entity_id.IsValid():
        raise RuntimeError(f"Could not create entity {name} under {parent_id}")
    editor.EditorEntityAPIBus(bus.Event, "SetName", entity_id, name)
    components.TransformBus(bus.Event, "SetLocalTranslation", entity_id, math.Vector3(*position))
    parent_records.append({"name": name, "parent_id": parent_id})
    entity_names_by_id[str(entity_id)] = name
    added = [component(entity_id, name) for name in components_to_add]
    return {
        "id": entity_id,
        "name": name,
        "components": added,
        "component_names": list(components_to_add),
        "named_components": dict(zip(components_to_add, added)),
    }


def add_lua(target, filename, names=(), gem_script=False):
    script_component = component(target["id"], "Lua Script")
    script_path = f"Scripts/ArenaShooter/{filename}" if gem_script else f"Scripts/Platformer/{filename}"
    asset_paths = [script_path.lower().replace(".lua", ".luac"), script_path, script_path.lower()]
    if gem_script:
        asset_paths.extend([
            f"Gems/ArenaShooter/Assets/Scripts/ArenaShooter/{filename}",
            f"scripts/arenashooter/{filename.lower().replace('.lua', '.luac')}",
        ])
    script_asset = asset_id(asset_paths)
    if script_asset is None:
        raise RuntimeError(f"Could not resolve script asset {script_path}")
    set_property(script_component, ["Script"], script_asset, required=True)
    if not editor.EditorComponentAPIBus(bus.Broadcast, "DisableComponents", [script_component]):
        raise RuntimeError(f"Could not disable Lua Script component for {script_path}")
    if not editor.EditorComponentAPIBus(bus.Broadcast, "EnableComponents", [script_component]):
        raise RuntimeError(f"Could not re-enable Lua Script component for {script_path}")
    wait_for_script(script_component, script_path, names)
    record = {
        "component": script_component,
        "entity": target["name"],
        "asset_hint": script_path.lower().replace(".lua", ".luac"),
    }
    script_records.append(record)
    return record


def set_lua_reference(script, name, target):
    if property_path(script["component"], [name]) is None:
        raise RuntimeError(f"Could not find Lua entity property {script['entity']}.{name}")
    pending_references.append((script["entity"], script["asset_hint"], name, target["name"]))


def set_scale(target, scale):
    outcome = editor.EditorComponentAPIBus(
        bus.Broadcast,
        "AddComponentOfType",
        target["id"],
        editor.EditorNonUniformScaleComponentTypeId,
    )
    if not outcome.IsSuccess():
        raise RuntimeError(f"Could not add non-uniform scale: {outcome.GetError()}")
    entity.NonUniformScaleRequestBus(bus.Event, "SetScale", target["id"], math.Vector3(*scale))


def set_rotation(target, rotation):
    components.TransformBus(bus.Event, "SetLocalRotation", target["id"], math.Vector3(*rotation))


def set_mesh(target, model_path, material_paths):
    model = asset_id([model_path])
    material = asset_id(material_paths)
    if model is None or material is None:
        raise RuntimeError(f"Could not resolve primitive assets {model_path} {material_paths}")
    mesh = component(target["id"], "Mesh")
    material_component = component(target["id"], "Material")
    if not set_property(mesh, ["Model Asset", "Model"], model, required=True):
        raise RuntimeError(f"Could not set model asset {model_path}")
    if not set_property(
        material_component,
        [
            "Material Asset",
            "Material",
            "Material Override",
            "Default Material",
            "MaterialSlots|Slots|0|MaterialAsset",
            "Material Slots|Slots|0|Material Asset",
        ],
        material,
        required=True,
    ):
        raise RuntimeError(f"Could not set primitive material {material_paths}")
    target["components"].extend([mesh, material_component])
    target["named_components"]["Mesh"] = mesh
    target["named_components"]["Material"] = material_component


BOX = "objects/_primitives/_box_1x1.fbx.azmodel"
BOX_MATERIAL = [
    "objects/_primitives/_box_1x1_middlegrey.azmaterial",
    "objects/_primitives/_box_1x1_middlegrey.material",
]
SPHERE = "objects/_primitives/_sphere_1x1.fbx.azmodel"
SPHERE_MATERIAL = [
    "objects/_primitives/_sphere_1x1_middlegrey.azmaterial",
    "objects/_primitives/_sphere_1x1_middlegrey.material",
]


def add_box(name, position, size, parent):
    names = ["PhysX Static Rigid Body", "PhysX Primitive Collider"]
    target = make_entity(name, position, parent, names)
    set_scale(target, size)
    set_mesh(target, BOX, BOX_MATERIAL)
    return target


def add_coin(name, position, parent):
    target = make_entity(name, position, parent)
    set_scale(target, (0.7, 0.7, 0.7))
    set_mesh(target, SPHERE, SPHERE_MATERIAL)
    spinner = component(target["id"], "${Name} Spinner")
    target["components"].append(spinner)
    script = add_lua(target, "Coin.lua", ["Radius"])
    set_lua_property(script, "Radius", 1.2)
    return target


def add_checkpoint(index, position, parent):
    target = make_entity(f"Checkpoint {index}", position, parent)
    script = add_lua(target, "Checkpoint.lua", ["Index", "Radius"])
    set_lua_property(script, "Index", float(index))
    add_box(
        f"Checkpoint {index} Beacon",
        (position[0], position[1], position[2] + 2.0),
        (0.35, 0.35, 4.0),
        parent,
    )
    return target


def add_goal(position, parent):
    target = make_entity("Goal Trigger", position, parent)
    script = add_lua(target, "Goal.lua", ["Radius"])
    set_lua_property(script, "Radius", 3.0)
    add_box("Goal Flag Pole", (0.0, 0.0, 2.5), (0.2, 0.2, 5.0), parent)
    add_box("Goal Flag", (0.8, 0.0, 4.1), (1.8, 0.2, 0.9), parent)
    return target


def add_moving_platform(name, position, size, parent, offset, period=4.0):
    target = make_entity(
        name, position, parent,
        ["PhysX Dynamic Rigid Body", "PhysX Primitive Collider"],
    )
    set_scale(target, size)
    set_mesh(target, BOX, BOX_MATERIAL)
    set_property(target["components"][0], ["Type"], True)
    script = add_lua(target, "MovingPlatform.lua", ["Offset", "Period"])
    set_lua_vector_property(script, "Offset", offset)
    set_lua_property(script, "Period", float(period))
    return target


def add_enemy(name, position, parent, patrol_offset=(4.0, 0.0, 0.0)):
    target = make_entity(
        name, position, parent,
        ["PhysX Character Controller", "PhysX Character Gameplay", "Behavior Tree"],
    )
    controller = target["components"][0]
    set_property(controller, ["Height"], 1.8, required=True)
    set_mesh(target, BOX, BOX_MATERIAL)
    tree = target["components"][2]
    set_property(
        tree, ["Tree file", "TreeFile"],
        "Scripts/Platformer/Enemy.behaviortree.json", required=True,
    )
    brain = add_lua(target, "EnemyBrain.lua", [
        "SightRange", "TouchRange", "Damage", "EyeHeight", "PatrolOffset",
    ])
    set_lua_vector_property(brain, "PatrolOffset", patrol_offset)
    health = add_lua(target, "Health.lua", ["MaxHealth", "SpawnPoint", "RespawnDelay"], True)
    set_lua_property(health, "MaxHealth", 1.0)
    return target


def add_voxel_volume(name, position, parent, size):
    volume = make_entity(name, position, parent, ["Voxel Nav Volume"])
    set_property(volume["components"][0], ["Size", "Settings|Size"], math.Vector3(*size), required=True)
    set_property(volume["components"][0], ["Voxel Size", "Settings|Voxel size"], 0.5, required=True)
    return volume


def add_policy(target, value=1):
    policy = component(target["id"], "World Partition Policy")
    target["components"].append(policy)
    set_property(policy, ["Policy"], value, required=True)


def add_group(name, world_position):
    group = make_entity(
        name, world_position, parent=streamed_parent_id,
    )
    add_policy(group, 2)
    return group


def set_player_input(player):
    input_asset = asset_id([
        "Gems/ArenaShooter/Assets/InputBindings/arenashooter.inputbindings",
        "InputBindings/arenashooter.inputbindings",
    ])
    if input_asset is None:
        raise RuntimeError("Could not resolve ArenaShooter input bindings")
    set_property(player["components"][2], ["Input to event bindings"], input_asset, required=True)


def clear_level_entities():
    roots = editor.EditorEntityInfoRequestBus(
        bus.Event, "GetChildren", level_root_entity_id
    )
    root_names = [
        editor.EditorEntityInfoRequestBus(bus.Event, "GetName", entity_id)
        for entity_id in roots
    ]
    print(
        f"Course level root {level_root_entity_id} has {len(roots)} children: {root_names}"
    )
    if roots:
        editor.ToolsApplicationRequestBus(
            bus.Broadcast, "DeleteEntitiesAndAllDescendants", roots
        )
    general.idle_wait_frames(2)
    remaining = editor.EditorEntityInfoRequestBus(
        bus.Event, "GetChildren", level_root_entity_id
    )
    if remaining:
        raise RuntimeError(f"Could not clear {len(remaining)} existing Course roots")
    print(f"Removed {len(roots)} existing level roots from Course")


def postprocess_prefab():
    prefab_path = os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", "..", "Levels", "Course", "Course.prefab")
    )
    with open(prefab_path, "r", encoding="utf-8") as handle:
        prefab = json.load(handle)
    container_id = prefab.get("ContainerEntity", {}).get("Id")
    entities_by_name = {
        item.get("Name"): item for item in prefab.get("Entities", {}).values() if item.get("Name")
    }
    if not container_id:
        raise RuntimeError("Course prefab has no container entity")

    parent_updates = 0
    for record in parent_records:
        entity_data = entities_by_name.get(record["name"])
        if entity_data is None:
            raise RuntimeError(f"Missing serialized entity {record['name']}")
        transform = next(
            (item for item in entity_data["Components"].values()
             if "TransformComponent" in item.get("$type", "")),
            None,
        )
        if transform is None or "Parent Entity" not in transform:
            raise RuntimeError(f"{record['name']} has no serialized Transform parent")
        parent_name = entity_names_by_id.get(str(record["parent_id"]))
        if parent_name in staging_entity_names:
            parent_id = container_id
        else:
            parent_id = entities_by_name[parent_name]["Id"] if parent_name else container_id
        if transform["Parent Entity"] != parent_id:
            transform["Parent Entity"] = parent_id
            parent_updates += 1

    def script_for(entity_name, asset_hint):
        owner = entities_by_name.get(entity_name)
        matches = [
            item for item in owner.get("Components", {}).values()
            if item.get("$type") == "ScriptEditorComponent"
            and item.get("ScriptAsset", {}).get("assetHint", "").lower() == asset_hint.lower()
        ] if owner else []
        if len(matches) != 1:
            raise RuntimeError(f"Expected one {asset_hint} script on {entity_name}; found {len(matches)}")
        return matches[0]

    for source_name, asset_hint, property_name, target_name in pending_references:
        script = script_for(source_name, asset_hint)
        properties = script.get("ScriptComponent", {}).get("Properties", {}).get("Properties", [])
        entry = next((item for item in properties if item.get("name") == property_name), None)
        if entry is None or not isinstance(entry.get("value"), dict) or "value" not in entry["value"]:
            raise RuntimeError(f"Editor did not serialize {source_name}.{property_name}")
        target = entities_by_name.get(target_name)
        if target is None:
            raise RuntimeError(f"Missing serialized reference target {target_name}")
        entry["value"]["value"] = target["Id"]

    for source_name, asset_hint, property_name, vector_value in pending_vector_properties:
        script = script_for(source_name, asset_hint)
        properties = script.get("ScriptComponent", {}).get("Properties", {}).get("Properties", [])
        entry = next((item for item in properties if item.get("name") == property_name), None)
        if entry is None or not isinstance(entry.get("value"), dict) or "value" not in entry["value"]:
            raise RuntimeError(f"Editor did not serialize {source_name}.{property_name}")
        entry["value"]["value"] = list(vector_value)

    staging_ids = {
        entities_by_name[name]["Id"]
        for name in staging_entity_names
        if name in entities_by_name
    }
    entities = prefab.get("Entities", {})
    for entity_key, entity_data in list(entities.items()):
        if entity_data.get("Id") in staging_ids:
            del entities[entity_key]

    direct_child_ids = []
    for entity_data in entities.values():
        for component_data in entity_data.get("Components", {}).values():
            if "TransformComponent" in component_data.get("$type", ""):
                if component_data.get("Parent Entity") == container_id:
                    direct_child_ids.append(entity_data["Id"])
                break
    sort_component = next(
        (
            component_data
            for component_data in prefab.get("ContainerEntity", {}).get("Components", {}).values()
            if component_data.get("$type") == "EditorEntitySortComponent"
            and "Child Entity Order" in component_data
        ),
        None,
    )
    if sort_component is None:
        raise RuntimeError("Course container has no serialized child order")
    sort_component["Child Entity Order"] = direct_child_ids

    temporary_path = prefab_path + ".tmp"
    with open(temporary_path, "w", encoding="utf-8") as handle:
        json.dump(prefab, handle, indent=4)
        handle.write("\n")
    os.replace(temporary_path, prefab_path)
    print(
        f"Updated existing Transform parent values for {parent_updates} entities and "
        f"{len(pending_references)} Editor-serialized Lua references; removed "
        f"{len(staging_ids)} staging roots from {prefab_path}"
    )


def save_level():
    path = os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", "..", "Levels", "Course", "Course.prefab")
    )
    previous = os.stat(path).st_mtime_ns if os.path.exists(path) else 0
    if not general.save_level():
        raise RuntimeError(f"Editor failed to save Course at {path}")
    deadline = time.monotonic() + 10.0
    while time.monotonic() < deadline:
        general.idle_wait(0.1)
        if os.path.exists(path) and os.stat(path).st_mtime_ns > previous:
            return
    raise RuntimeError(f"Timed out waiting for Course prefab at {path}")


def main():
    global always_loaded_parent_id, level_root_entity_id, streamed_parent_id
    general.idle_enable(True)
    print("Starting Platformer Course generation")
    create_result = general.create_level_no_prompt(
        "DefaultLevel", "Course", 1024, 1, 4096, False
    )
    print(f"create_level_no_prompt returned {create_result}")
    if not general.open_level_no_prompt("Course"):
        raise RuntimeError("Could not open Course")
    general.idle_wait_frames(60)
    level_root_entity_id = editor.ToolsApplicationRequestBus(bus.Broadcast, "GetCurrentLevelEntityId")
    if not level_root_entity_id.IsValid():
        raise RuntimeError("Could not get Course level root")
    clear_level_entities()

    box_asset = asset_id([BOX])
    box_material = asset_id(BOX_MATERIAL)
    sphere_asset = asset_id([SPHERE])
    sphere_material = asset_id(SPHERE_MATERIAL)
    if not all([box_asset, box_material, sphere_asset, sphere_material]):
        raise RuntimeError("Could not resolve primitive course assets")

    always_loaded_staging = make_entity(
        staging_entity_names[0], parent=level_root_entity_id
    )
    streamed_staging = make_entity(
        staging_entity_names[1], parent=level_root_entity_id
    )
    always_loaded_parent_id = always_loaded_staging["id"]
    streamed_parent_id = streamed_staging["id"]

    world = make_entity(
        "World", parent=always_loaded_parent_id, components_to_add=["World Partition"]
    )
    partition = world["components"][0]
    for names, value in [
        (["Cell size"], 64.0),
        (["Load radius"], 128.0),
        (["Unload radius"], 160.0),
        (["2D grid"], True),
        (["Stream by default"], True),
    ]:
        set_property(partition, names, value, required=True)
    add_policy(world)

    sun = make_entity(
        "Sun", parent=always_loaded_parent_id, components_to_add=["Directional Light"]
    )
    set_rotation(sun, (-0.8726646, 0.0, 0.5235988))
    set_property(sun["components"][0], ["Enable Shadow"], True, required=True)
    add_policy(sun)

    skybox = asset_id(["lightingpresets/highcontrast/goegap_4k_skyboxcm.exr.streamingimage"])
    diffuse = asset_id(["lightingpresets/highcontrast/goegap_4k_iblglobalcm_ibldiffuse.exr.streamingimage"])
    specular = asset_id(["lightingpresets/highcontrast/goegap_4k_iblglobalcm_iblspecular.exr.streamingimage"])
    if not all([skybox, diffuse, specular]):
        raise RuntimeError("Could not resolve DefaultProject lighting cubemaps")
    sky = make_entity(
        "Sky", parent=always_loaded_parent_id,
        components_to_add=["HDRi Skybox", "Global Skylight (IBL)"],
    )
    set_property(sky["components"][0], ["Cubemap Texture", "CubemapAsset"], skybox, required=True)
    set_property(sky["components"][1], ["Diffuse Image", "diffuseImageAsset"], diffuse, required=True)
    set_property(sky["components"][1], ["Specular Image", "specularImageAsset"], specular, required=True)
    add_policy(sky)

    player = make_entity(
        "Player", (0.0, 0.0, 0.0), parent=always_loaded_parent_id,
        components_to_add=[
            "PhysX Character Controller", "PhysX Character Gameplay", "Input",
            "World Partition Streaming Source",
        ],
    )
    set_property(player["components"][0], ["Height"], 1.8, required=True)
    set_player_input(player)

    body = make_entity("Body", (0.0, 0.0, 0.9), player["id"])
    set_scale(body, (0.65, 0.45, 1.5))
    set_mesh(body, BOX, BOX_MATERIAL)
    pivot = make_entity("Camera Pivot", (0.0, 0.0, 1.5), player["id"])
    camera = make_entity("Camera", (0.0, -7.0, 2.0), pivot["id"], ["Camera"])
    set_rotation(camera, (-0.25, 0.0, 0.0))

    controller = add_lua(player, "PlatformerController.lua", [
        "MoveSpeed", "JumpSpeed", "MaxJumps", "LookSensitivity", "MaxPitch", "Body", "CameraPivot",
    ])
    set_lua_reference(controller, "Body", body)
    set_lua_reference(controller, "CameraPivot", pivot)
    life = add_lua(player, "PlayerLife.lua", ["MaxHealth", "KillZ"])
    set_lua_property(life, "MaxHealth", 3.0)
    set_lua_property(life, "KillZ", -20.0)
    add_lua(player, "PlatformerHud.lua")
    add_lua(player, "DeviceDetector.lua", ["ShowOnScreen"], True)
    settings = add_lua(player, "SettingsMenu.lua", ["CameraEntity"], True)
    set_lua_reference(settings, "CameraEntity", camera)
    add_policy(player)

    start = add_group("Start and Tutorial", (0.0, 0.0, 0.0))
    add_box("Start Platform", (0.0, 0.0, -0.5), (16.0, 20.0, 1.0), start["id"])
    for index, position in enumerate([
        (-4.0, 14.0, 0.3), (2.0, 21.0, 0.8), (-2.0, 28.0, 1.3), (4.0, 35.0, 1.8),
    ], 1):
        add_box(f"Tutorial Jump {index}", position, (4.0, 4.0, 0.6), start["id"])
    for index, position in enumerate([(0.0, 4.0, 2.0), (-3.0, 16.0, 2.0), (3.0, 30.0, 3.0)], 1):
        add_coin(f"Coin {index:02d}", position, start["id"])

    gap = add_group("Floating Platform Gap Run", (0.0, 42.0, 0.0))
    for index, position in enumerate([
        (-3.0, 0.0, 0.0), (3.0, 8.0, 0.7), (-3.0, 16.0, 1.4),
        (3.0, 24.0, 0.7), (0.0, 32.0, 0.0),
    ], 1):
        add_box(f"Gap Platform {index}", position, (5.0, 5.0, 0.6), gap["id"])
    for index, position in enumerate([(0.0, 4.0, 2.3), (3.0, 13.0, 3.0), (-3.0, 22.0, 3.0)], 4):
        add_coin(f"Coin {index:02d}", position, gap["id"])

    zone1 = add_group("Zone 1", (0.0, 86.0, 0.0))
    add_box("Zone 1 Platform", (0.0, 0.0, -0.5), (30.0, 24.0, 1.0), zone1["id"])
    add_voxel_volume("Zone 1 VoxelNav", (0.0, 0.0, 8.0), zone1["id"], (40.0, 34.0, 20.0))
    add_enemy("Zone 1 Enemy 1", (-8.0, 0.0, 0.0), zone1["id"])
    add_enemy("Zone 1 Enemy 2", (8.0, 0.0, 0.0), zone1["id"])
    for index, position in enumerate([(-10.0, 7.0, 2.0), (0.0, 8.0, 2.0), (10.0, 7.0, 2.0)], 7):
        add_coin(f"Coin {index:02d}", position, zone1["id"])

    moving = add_group("Horizontal Moving Platform Run", (0.0, 116.0, 0.0))
    add_box("Moving Run Approach", (0.0, -11.0, -0.5), (8.0, 8.0, 1.0), moving["id"])
    add_box("Moving Run Start", (0.0, 0.0, -0.5), (12.0, 8.0, 1.0), moving["id"])
    add_moving_platform("Moving Platform 1", (0.0, 12.0, 0.0), (5.0, 5.0, 0.6), moving["id"], (5.0, 0.0, 0.0))
    add_moving_platform("Moving Platform 2", (0.0, 24.0, 1.0), (5.0, 5.0, 0.6), moving["id"], (-5.0, 0.0, 0.0))
    add_box("Moving Run End", (0.0, 36.0, -0.5), (12.0, 8.0, 1.0), moving["id"])
    for index, position in enumerate([(-2.0, 9.0, 2.0), (2.0, 21.0, 3.0), (0.0, 32.0, 2.0)], 10):
        add_coin(f"Coin {index:02d}", position, moving["id"])

    checkpoint1 = add_group("Checkpoint 1 Area", (0.0, 158.0, 0.0))
    add_box("Checkpoint 1 Platform", (0.0, 0.0, -0.5), (14.0, 12.0, 1.0), checkpoint1["id"])
    add_checkpoint(1, (0.0, 0.0, 1.0), checkpoint1["id"])
    for index, position in enumerate([(-3.0, 3.0, 3.0), (3.0, 4.0, 3.0)], 13):
        add_coin(f"Coin {index:02d}", position, checkpoint1["id"])

    lifts = add_group("Vertical Lifts", (0.0, 168.0, 0.0))
    add_box("Lift Start", (0.0, 0.0, -0.5), (12.0, 8.0, 1.0), lifts["id"])
    add_moving_platform("Vertical Lift 1", (0.0, 12.0, 0.0), (5.0, 5.0, 0.6), lifts["id"], (0.0, 0.0, 4.0), 5.0)
    add_moving_platform("Vertical Lift 2", (0.0, 24.0, 4.0), (5.0, 5.0, 0.6), lifts["id"], (0.0, 0.0, 4.0), 5.0)
    add_box("Lift End", (0.0, 36.0, 8.0), (12.0, 8.0, 1.0), lifts["id"])
    for index, position in enumerate([(0.0, 11.0, 3.0), (0.0, 23.0, 7.0)], 15):
        add_coin(f"Coin {index:02d}", position, lifts["id"])

    high_section = add_group("High Section", (0.0, 214.0, 8.0))
    add_box("High Section Platform", (0.0, 0.0, -0.5), (14.0, 12.0, 1.0), high_section["id"])

    zone2 = add_group("Zone 2", (0.0, 242.0, 8.0))
    add_box("Zone 2 Platform", (0.0, 0.0, -0.5), (34.0, 28.0, 1.0), zone2["id"])
    add_voxel_volume("Zone 2 VoxelNav", (0.0, 0.0, 8.0), zone2["id"], (44.0, 38.0, 20.0))
    add_enemy("Zone 2 Enemy 1", (-9.0, 0.0, 0.0), zone2["id"])
    add_enemy("Zone 2 Enemy 2", (0.0, 0.0, 0.0), zone2["id"])
    add_enemy("Zone 2 Enemy 3", (9.0, 0.0, 0.0), zone2["id"])
    for index, position in enumerate([(-12.0, 8.0, 2.0), (0.0, 9.0, 2.0), (12.0, 8.0, 2.0)], 17):
        add_coin(f"Coin {index:02d}", position, zone2["id"])

    double_jump = add_group("Double Jump Gap Series", (0.0, 274.0, 0.0))
    for index, position in enumerate([
        (0.0, 0.0, 0.0), (-3.0, 9.0, 1.0), (3.0, 18.0, 2.0),
        (-3.0, 27.0, 2.0), (3.0, 36.0, 1.0), (0.0, 45.0, 0.0),
    ], 1):
        add_box(f"Double Jump Platform {index}", position, (4.0, 4.0, 0.6), double_jump["id"])
    for index, position in enumerate([(-1.0, 7.0, 3.0), (2.0, 16.0, 4.0), (-1.0, 34.0, 3.0)], 20):
        add_coin(f"Coin {index:02d}", position, double_jump["id"])

    checkpoint2 = add_group("Checkpoint 2 Area", (0.0, 330.0, 0.0))
    add_box("Checkpoint 2 Platform", (0.0, 0.0, -0.5), (14.0, 12.0, 1.0), checkpoint2["id"])
    add_checkpoint(2, (0.0, 0.0, 1.0), checkpoint2["id"])
    for index, position in enumerate([(-3.0, 3.0, 3.0), (3.0, 4.0, 3.0)], 23):
        add_coin(f"Coin {index:02d}", position, checkpoint2["id"])

    ramp = add_group("Ramp Descent", (0.0, 350.0, 0.0))
    for index in range(7):
        block = add_box(
            f"Descent Ramp {index + 1}",
            (0.0, index * 7.0, -index * 1.0),
            (9.0, 9.0, 0.8),
            ramp["id"],
        )
        set_rotation(block, (-0.12, 0.0, 0.0))
    for index, position in enumerate([(-2.0, 12.0, 2.0), (2.0, 32.0, -1.0)], 25):
        add_coin(f"Coin {index:02d}", position, ramp["id"])

    zone3 = add_group("Zone 3", (0.0, 416.0, -6.0))
    add_box("Zone 3 Platform", (0.0, 0.0, -0.5), (30.0, 24.0, 1.0), zone3["id"])
    add_voxel_volume("Zone 3 VoxelNav", (0.0, 0.0, 8.0), zone3["id"], (40.0, 34.0, 20.0))
    add_enemy("Zone 3 Enemy 1", (-8.0, 0.0, 0.0), zone3["id"])
    add_enemy("Zone 3 Enemy 2", (8.0, 0.0, 0.0), zone3["id"])
    for index, position in enumerate([(-10.0, 7.0, 2.0), (0.0, 8.0, 2.0), (10.0, 7.0, 2.0)], 27):
        add_coin(f"Coin {index:02d}", position, zone3["id"])

    final1 = add_group("Final Gauntlet 1", (0.0, 438.0, -6.0))
    for index, position in enumerate([
        (0.0, 0.0, 0.0), (-2.0, 8.0, 0.5), (2.0, 16.0, 1.0),
        (-2.0, 24.0, 0.5), (2.0, 32.0, 0.0), (0.0, 40.0, 0.0), (0.0, 48.0, 0.0),
    ], 1):
        add_box(f"Final Platform 1-{index}", position, (4.5, 4.5, 0.6), final1["id"])

    final2 = add_group("Final Gauntlet 2", (0.0, 494.0, -6.0))
    for index, position in enumerate([
        (0.0, 0.0, 0.0), (2.0, 8.0, 0.8), (-2.0, 16.0, 1.6),
        (2.0, 24.0, 0.8), (-2.0, 32.0, 0.0), (0.0, 40.0, 0.0), (0.0, 48.0, 0.0),
    ], 1):
        add_box(f"Final Platform 2-{index}", position, (4.5, 4.5, 0.6), final2["id"])

    final3 = add_group("Final Gauntlet 3", (0.0, 550.0, -6.0))
    for index, position in enumerate([
        (0.0, 0.0, 0.0), (-2.0, 8.0, 0.8), (2.0, 16.0, 1.6),
        (-2.0, 24.0, 0.8), (2.0, 32.0, 0.0), (0.0, 40.0, 0.0), (0.0, 48.0, 0.0),
    ], 1):
        add_box(f"Final Platform 3-{index}", position, (4.5, 4.5, 0.6), final3["id"])
    add_coin("Coin 30", (0.0, 28.0, 3.0), final3["id"])

    final4 = add_group("Final Gauntlet 4", (0.0, 606.0, -6.0))
    for index, position in enumerate([(0.0, 0.0, 0.0), (0.0, 8.0, 0.0), (0.0, 16.0, 0.0)], 1):
        add_box(f"Final Platform 4-{index}", position, (6.0, 6.0, 0.6), final4["id"])

    goal = add_group("Goal Area", (0.0, 626.0, -6.0))
    add_box("Goal Platform", (0.0, 0.0, -0.5), (18.0, 16.0, 1.0), goal["id"])
    add_goal((0.0, 4.0, 1.0), goal["id"])

    save_level()
    postprocess_prefab()
    print("Generated Platformer Course with three VoxelNav enemy zones, checkpoints, and 30 coins")


try:
    main()
except Exception as error:
    print(f"Course generation failed: {error}")
    raise
finally:
    general.exit_no_prompt()
