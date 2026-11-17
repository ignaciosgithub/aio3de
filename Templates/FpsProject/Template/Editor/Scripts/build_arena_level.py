# Copyright (c) Contributors to the Open 3D Engine Project.
# For complete copyright and license terms please see the LICENSE at the root of this distribution.
# SPDX-License-Identifier: Apache-2.0 OR MIT

import azlmbr.bus as bus
import azlmbr.asset as asset
import azlmbr.components as components
import azlmbr.editor as editor
import azlmbr.entity as entity
import azlmbr.legacy.general as general
import azlmbr.math as math
import json
import os
import zlib


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


def component_property_path(component_id, names):
    paths = editor.EditorComponentAPIBus(bus.Broadcast, "BuildComponentPropertyList", component_id)
    for name in names:
        suffix = name.lower()
        for path in paths:
            if path.lower() == suffix or path.lower().endswith("|" + suffix):
                return path
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


def asset_id(paths):
    for path in paths:
        result = asset.AssetCatalogRequestBus(bus.Broadcast, "GetAssetIdByPath", path, math.Uuid(), False)
        if result.is_valid():
            return result
    return None


def make_entity(name, position, scale=1.0, component_names=(), parent=None):
    parent_id = parent if parent is not None else entity.EntityId()
    entity_id = editor.ToolsApplicationRequestBus(bus.Broadcast, "CreateNewEntity", parent_id)
    if not entity_id.IsValid():
        raise RuntimeError(f"Could not create entity {name}")
    editor.EditorEntityAPIBus(bus.Event, "SetName", entity_id, name)
    components.TransformBus(
        bus.Event,
        "SetWorldTranslation",
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


def add_lua(entity, filename, gem_script=False):
    script = component(entity["id"], "Lua Script")
    path = f"Scripts/ArenaShooter/{filename}" if gem_script else f"Scripts/Fps/{filename}"
    product_path = path.lower().replace(".lua", ".luac")
    asset_paths = [path, path.lower(), product_path]
    if gem_script:
        asset_paths.append(f"Gems/ArenaShooter/Assets/Scripts/ArenaShooter/{filename}")
        asset_paths.append(f"scripts/arenashooter/{filename.lower().replace('.lua', '.luac')}")
    script_id = asset_id(asset_paths)
    if script_id is None:
        raise RuntimeError(f"Could not resolve script asset {path}")
    set_property(script, ["Script"], script_id, required=True)
    return script


def entity_reference(entity_id):
    return {"$type": "AZ::EntityId", "value": entity_id}


def script_property(name, value, entities):
    property_id = zlib.crc32(name.encode("utf-8"))
    def entity_id(entity):
        if isinstance(entity, dict):
            return entities[entity["name"]]["Id"]
        return entity

    if isinstance(value, (int, float)):
        return {
            "$type": "AzFramework::ScriptPropertyNumber",
            "id": property_id,
            "name": name,
            "value": float(value),
        }
    if isinstance(value, list):
        return {
            "$type": "AZ::ScriptPropertyGenericClassArray",
            "id": property_id,
            "name": name,
            "values": [entity_reference(entity_id(entity)) for entity in value],
            "elementType": "{6383F1D3-BB27-4E6B-A49A-6409B2059EAA}",
        }
    return {
        "$type": "AzFramework::ScriptPropertyGenericClass",
        "id": property_id,
        "name": name,
        "value": entity_reference(entity_id(value)),
    }


def postprocess_script_properties(prefab_path, overrides):
    with open(prefab_path, "r", encoding="utf-8") as prefab_file:
        prefab = json.load(prefab_file)

    entities = {}

    def find_entities(value):
        if isinstance(value, dict):
            if "Name" in value and "Components" in value:
                entities[value["Name"]] = value
            for child in value.values():
                find_entities(child)
        elif isinstance(value, list):
            for child in value:
                find_entities(child)

    find_entities(prefab)
    for entity_name, script_name, property_name, property_value in overrides:
        target = entities.get(entity_name)
        if target is None:
            raise RuntimeError(f"Could not find entity {entity_name} in saved Arena prefab")
        expected_hint = script_name.lower().replace(".lua", ".luac")
        matching_scripts = []
        for component_data in target["Components"].values():
            if component_data.get("$type") != "ScriptEditorComponent":
                continue
            script_data = component_data.get("ScriptComponent", {}).get("Script", {})
            if script_data.get("assetHint", "").lower().endswith(expected_hint):
                matching_scripts.append(component_data)
        if len(matching_scripts) != 1:
            raise RuntimeError(
                f"Expected one {script_name} component on {entity_name}, found {len(matching_scripts)}"
            )
        properties = matching_scripts[0].setdefault("ScriptComponent", {}).setdefault("Properties", {}).setdefault(
            "Properties", []
        )
        properties[:] = [entry for entry in properties if entry.get("name") != property_name]
        properties.append(script_property(property_name, property_value, entities))

    with open(prefab_path, "w", encoding="utf-8") as prefab_file:
        json.dump(prefab, prefab_file, indent=4)
        prefab_file.write("\n")


def main():
    create_result = general.create_level_no_prompt("DefaultLevel", "Arena", 1024, 1, 4096, False)
    print(f"create_level_no_prompt returned {create_result}")
    open_result = general.open_level_no_prompt("Arena")
    if not open_result:
        raise RuntimeError("Could not open Arena")

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

    player = make_entity(
        "Player", (0.0, -22.0, 1.0), component_names=[
            "PhysX Character Controller", "PhysX Character Gameplay", "Input",
        ],
    )
    input_asset = asset_id(["Gems/ArenaShooter/Assets/InputBindings/arenashooter.inputbindings",
                            "InputBindings/arenashooter.inputbindings"])
    if input_asset is None:
        raise RuntimeError("Could not resolve ArenaShooter input bindings")
    set_property(player["components"][2], ["Input to event bindings"], input_asset, required=True)
    camera = make_entity("Player Camera", (0.0, 0.0, 1.6), parent=player["id"], component_names=["Camera"])
    components.TransformBus(bus.Event, "SetLocalTranslation", camera["id"], math.Vector3(0.0, 0.0, 1.6))
    player_controller = add_lua(player, "PlayerController.lua", True)
    weapon_a = add_lua(player, "Weapon.lua", True)
    weapon_switcher = add_lua(player, "WeaponSwitcher.lua", True)
    add_lua(player, "Health.lua", True)
    script_overrides = [
        (player["name"], "PlayerController.lua", "CameraEntity", camera),
        (player["name"], "Weapon.lua", "CameraEntity", camera),
        (player["name"], "Weapon.lua", "Slot", 1.0),
        (player["name"], "WeaponSwitcher.lua", "WeaponCount", 1.0),
    ]

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
        add_lua(enemy, "EnemyBrain.lua")
        script_overrides.append((enemy["name"], "EnemyBrain.lua", "Player", player))
        add_lua(enemy, "Health.lua", True)
        enemies.append(enemy)

    manager = make_entity("Game Manager", (0.0, 0.0, 0.0), component_names=["Input"])
    manager_input = asset_id(["Gems/ArenaShooter/Assets/InputBindings/arenashooter.inputbindings",
                              "InputBindings/arenashooter.inputbindings"])
    if manager_input is None:
        raise RuntimeError("Could not resolve ArenaShooter input bindings for Game Manager")
    set_property(manager["components"][0], ["Input to event bindings"], manager_input, required=True)
    score = add_lua(manager, "ScoreHud.lua", True)
    add_lua(manager, "DeviceDetector.lua", True)
    add_lua(manager, "SettingsMenu.lua", True)
    script_overrides.extend([
        (manager["name"], "ScoreHud.lua", "Targets", enemies),
        (manager["name"], "SettingsMenu.lua", "CameraEntity", camera),
        (player["name"], "PlayerController.lua", "DeviceDetectorEntity", manager),
    ])

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

    general.save_level()
    project_path = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    prefab_path = os.path.join(project_path, "Levels", "Arena", "Arena.prefab")
    postprocess_script_properties(prefab_path, script_overrides)
    print(f"Generated FPS arena with {len(enemies)} BehaviorTree enemies")


try:
    main()
except Exception as error:
    print(f"Arena generation failed: {error}")
    general.exit_no_prompt()
    raise
else:
    general.exit_no_prompt()
