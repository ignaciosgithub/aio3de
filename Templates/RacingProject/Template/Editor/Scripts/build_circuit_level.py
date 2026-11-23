# Copyright (c) Contributors to the Open 3D Engine Project.
# For complete copyright and license terms please see the LICENSE at the root of this distribution.
# SPDX-License-Identifier: Apache-2.0 OR MIT

import azlmbr.asset as asset
import azlmbr.bus as bus
import azlmbr.components as components
import azlmbr.editor as editor
import azlmbr.entity as entity
import azlmbr.legacy.general as general
import azlmbr.math as math
import json
import math as pymath
import os
import random
import time

CONTROL_POINTS = [
    (-198.0, -84.0),
    (-54.0, -100.8),
    (135.0, -100.8),
    (207.0, -75.6),
    (207.0, -46.2),
    (171.0, -33.6),
    (63.0, -37.8),
    (31.5, -16.8),
    (36.0, 12.6),
    (162.0, 25.2),
    (207.0, 54.6),
    (198.0, 88.2),
    (135.0, 105.0),
    (9.0, 96.6),
    (-144.0, 79.8),
    (-211.5, 46.2),
    (-211.5, 8.4),
    (-180.0, -21.0),
]
TRACK_WIDTH = 14.0
SAMPLE_SPACING = 6.0
BOX = "objects/_primitives/_box_1x1.fbx.azmodel"
SPHERE = "objects/_primitives/_sphere_1x1.fbx.azmodel"
CYLINDER = "objects/_primitives/_cylinder_1x1.fbx.azmodel"
MATERIAL_NAMES = (
    "grass",
    "asphalt",
    "red",
    "white",
    "black",
    "bark",
    "leaves",
    "gold",
    "blue",
    "yellow",
)

level_root_entity_id = None
always_loaded_parent_id = None
streamed_parent_id = None
parent_records = []
entity_names_by_id = {}
script_records = []
pending_references = []
pending_vector_properties = []
entity_count = 0
trophy_entity = None


def component(entity_id, name, entity_type=None):
    if entity_type is None:
        entity_type = entity.EntityType().Game
    type_ids = editor.EditorComponentAPIBus(
        bus.Broadcast, "FindComponentTypeIdsByEntityType", [name], entity_type
    )
    if not type_ids:
        raise RuntimeError(f"Missing Editor component type: {name}")
    outcome = editor.EditorComponentAPIBus(
        bus.Broadcast, "AddComponentsOfType", entity_id, type_ids
    )
    if not outcome.IsSuccess():
        raise RuntimeError(f"Could not add component {name}: {outcome.GetError()}")
    return outcome.GetValue()[0]


def property_path(component_id, names):
    paths = editor.EditorComponentAPIBus(
        bus.Broadcast, "BuildComponentPropertyList", component_id
    )
    for name in names:
        suffix = name.lower()
        matches = [
            path
            for path in paths
            if path.lower() == suffix or path.lower().endswith("|" + suffix)
        ]
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


def asset_id(paths):
    for path in paths:
        result = asset.AssetCatalogRequestBus(
            bus.Broadcast, "GetAssetIdByPath", path, math.Uuid(), False
        )
        if result.is_valid():
            return result
    return None


def wait_for_asset(paths, timeout=45.0):
    cache = getattr(wait_for_asset, "cache", {})
    key = tuple(paths)
    if key in cache:
        return cache[key]
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = asset_id(paths)
        if result is not None:
            cache[key] = result
            wait_for_asset.cache = cache
            return result
        general.idle_wait(0.1)
    raise RuntimeError(f"Timed out waiting for asset {paths}")


def make_entity(name, position=(0.0, 0.0, 0.0), parent=None, components_to_add=()):
    global entity_count
    parent_id = parent if parent is not None else level_root_entity_id
    entity_id = editor.ToolsApplicationRequestBus(
        bus.Broadcast, "CreateNewEntity", parent_id
    )
    if not entity_id.IsValid():
        raise RuntimeError(f"Could not create entity {name} under {parent_id}")
    editor.EditorEntityAPIBus(bus.Event, "SetName", entity_id, name)
    components.TransformBus(
        bus.Event, "SetLocalTranslation", entity_id, math.Vector3(*position)
    )
    parent_records.append({"name": name, "parent_id": parent_id})
    entity_names_by_id[str(entity_id)] = name
    added = [component(entity_id, item) for item in components_to_add]
    entity_count += 1
    if entity_count % 80 == 0:
        general.idle_wait_frames(1)
    return {
        "id": entity_id,
        "name": name,
        "components": added,
        "component_names": list(components_to_add),
        "named_components": dict(zip(components_to_add, added)),
    }


def add_lua(target, filename, names=(), gem_script=False):
    script_component = component(target["id"], "Lua Script")
    if gem_script:
        script_path = f"Scripts/ArenaShooter/{filename}"
        paths = [
            f"{script_path.lower().replace('.lua', '.luac')}",
            script_path,
            script_path.lower(),
            f"Gems/ArenaShooter/Assets/Scripts/ArenaShooter/{filename}",
        ]
    else:
        script_path = f"Scripts/Racing/{filename}"
        paths = [
            script_path.lower().replace(".lua", ".luac"),
            script_path,
            script_path.lower(),
        ]
    script_asset = wait_for_asset(paths)
    set_property(script_component, ["Script"], script_asset, required=True)
    if not editor.EditorComponentAPIBus(
        bus.Broadcast, "DisableComponents", [script_component]
    ):
        raise RuntimeError(f"Could not disable Lua Script component for {script_path}")
    if not editor.EditorComponentAPIBus(
        bus.Broadcast, "EnableComponents", [script_component]
    ):
        raise RuntimeError(f"Could not re-enable Lua Script component for {script_path}")
    deadline = time.monotonic() + 15.0
    last_paths = []
    while time.monotonic() < deadline:
        last_paths = editor.EditorComponentAPIBus(
            bus.Broadcast, "BuildComponentPropertyList", script_component
        )
        if all(
            any(
                item.lower() == name.lower()
                or item.lower().endswith("|" + name.lower())
                for item in last_paths
            )
            for name in names
        ):
            break
        general.idle_wait(0.1)
    else:
        raise RuntimeError(
            f"Timed out waiting for {script_path} properties {names}; got {last_paths}"
        )
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
    pending_references.append(
        (script["entity"], script["asset_hint"], name, target["name"])
    )


def set_scale(target, scale):
    outcome = editor.EditorComponentAPIBus(
        bus.Broadcast,
        "AddComponentOfType",
        target["id"],
        editor.EditorNonUniformScaleComponentTypeId,
    )
    if not outcome.IsSuccess():
        raise RuntimeError(f"Could not add non-uniform scale: {outcome.GetError()}")
    entity.NonUniformScaleRequestBus(
        bus.Event, "SetScale", target["id"], math.Vector3(*scale)
    )


def set_rotation(target, rotation):
    components.TransformBus(
        bus.Event, "SetLocalRotation", target["id"], math.Vector3(*rotation)
    )


def material_paths(name):
    return [
        f"assets/materials/racing_{name}.azmaterial",
        f"assets/materials/racing_{name}.material",
    ]


def set_mesh(target, model_path, materials):
    model = wait_for_asset([model_path])
    material = wait_for_asset(materials)
    mesh = component(target["id"], "Mesh")
    material_component = component(target["id"], "Material")
    set_property(mesh, ["Model Asset", "Model"], model, required=True)
    set_property(
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
    )
    target["components"].extend([mesh, material_component])
    target["named_components"]["Mesh"] = mesh
    target["named_components"]["Material"] = material_component


def add_visual_box(name, world_position, size, parent, anchor, material, yaw=0.0):
    local_position = tuple(world_position[i] - anchor[i] for i in range(3))
    target = make_entity(name, local_position, parent)
    set_scale(target, size)
    set_rotation(target, (0.0, 0.0, yaw))
    set_mesh(target, BOX, material_paths(material))
    return target


def add_visual_mesh(name, world_position, size, parent, anchor, model, material):
    local_position = tuple(world_position[i] - anchor[i] for i in range(3))
    target = make_entity(name, local_position, parent)
    set_scale(target, size)
    set_mesh(target, model, material_paths(material))
    return target


def add_policy(target, value=1):
    policy = component(target["id"], "World Partition Policy")
    target["components"].append(policy)
    target["named_components"]["World Partition Policy"] = policy
    set_property(policy, ["Policy"], value, required=True)


def add_group(name, anchor):
    group = make_entity(name, anchor, parent=streamed_parent_id)
    add_policy(group, 2)
    return group


def catmull_rom(p0, p1, p2, p3, t):
    t2 = t * t
    t3 = t2 * t
    return tuple(
        0.5
        * (
            2.0 * p1[axis]
            + (-p0[axis] + p2[axis]) * t
            + (2.0 * p0[axis] - 5.0 * p1[axis] + 4.0 * p2[axis] - p3[axis])
            * t2
            + (-p0[axis] + 3.0 * p1[axis] - 3.0 * p2[axis] + p3[axis])
            * t3
        )
        for axis in range(2)
    )


def generate_track():
    dense = []
    subdivisions = 80
    for index, p1 in enumerate(CONTROL_POINTS):
        p0 = CONTROL_POINTS[(index - 1) % len(CONTROL_POINTS)]
        p2 = CONTROL_POINTS[(index + 1) % len(CONTROL_POINTS)]
        p3 = CONTROL_POINTS[(index + 2) % len(CONTROL_POINTS)]
        for step in range(subdivisions + 1):
            if index == len(CONTROL_POINTS) - 1 and step == subdivisions:
                continue
            dense.append(catmull_rom(p0, p1, p2, p3, step / subdivisions))

    lengths = [0.0]
    for index in range(1, len(dense)):
        lengths.append(lengths[-1] + pymath.dist(dense[index - 1], dense[index]))
    total_length = lengths[-1] + pymath.dist(dense[-1], dense[0])
    sample_count = round(total_length / SAMPLE_SPACING)
    if not 1200.0 <= total_length <= 1500.0 or not 200 <= sample_count <= 250:
        raise RuntimeError(
            f"Circuit length/sample count outside brief: {total_length:.2f} m/{sample_count}"
        )

    points = []
    segment = 1
    for sample in range(sample_count):
        target_distance = total_length * sample / sample_count
        while segment < len(lengths) - 1 and lengths[segment] < target_distance:
            segment += 1
        start = dense[segment - 1]
        end = dense[segment]
        span = lengths[segment] - lengths[segment - 1]
        fraction = (
            (target_distance - lengths[segment - 1]) / span if span > 0.0 else 0.0
        )
        points.append(
            (
                start[0] + (end[0] - start[0]) * fraction,
                start[1] + (end[1] - start[1]) * fraction,
                0.0,
            )
        )
    return points, total_length


def write_track_data(points):
    path = os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", "..", "Scripts", "Racing", "TrackData.lua")
    )
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("--\n")
        handle.write("-- Copyright (c) Contributors to the Open 3D Engine Project.\n")
        handle.write("-- For complete copyright and license terms please see the LICENSE at the root of this distribution.\n")
        handle.write("--\n")
        handle.write("-- SPDX-License-Identifier: Apache-2.0 OR MIT\n")
        handle.write("--\n")
        handle.write("-- Generated by Editor/Scripts/build_circuit_level.py; do not edit by hand.\n")
        handle.write("return {\n    width = 14.0,\n    points = {\n")
        for x, y, _ in points:
            handle.write(f"        {{x = {x:.2f}, y = {y:.2f}, z = 0.0}},\n")
        handle.write("    },\n    startIndex = 0\n}\n")
    print(f"Wrote {len(points)} generated track points to {path}")


def distance2(a, b):
    return pymath.hypot(b[0] - a[0], b[1] - a[1])


def tangent(points, index):
    count = len(points)
    before = points[(index - 1) % count]
    after = points[(index + 1) % count]
    dx = after[0] - before[0]
    dy = after[1] - before[1]
    length = pymath.hypot(dx, dy)
    return dx / length, dy / length


def point_at_distance_back(points, start_index, distance):
    index = start_index
    position = points[index]
    remaining = distance
    while remaining > 0.0:
        previous = points[(index - 1) % len(points)]
        span = distance2(previous, position)
        if remaining <= span:
            fraction = remaining / span
            return (
                position[0] + (previous[0] - position[0]) * fraction,
                position[1] + (previous[1] - position[1]) * fraction,
                0.0,
            )
        remaining -= span
        index = (index - 1) % len(points)
        position = points[index]
    return position


def input_asset():
    paths = [
        "Gems/ArenaShooter/Assets/InputBindings/arenashooter.inputbindings",
        "InputBindings/arenashooter.inputbindings",
    ]
    return wait_for_asset(paths)


def set_player_input(player):
    set_property(
        player["named_components"]["Input"],
        ["Input to event bindings"],
        input_asset(),
        required=True,
    )


def add_ground(anchor):
    ground = make_entity(
        "Ground",
        (0.0, 0.0, -0.5),
        parent=always_loaded_parent_id,
        components_to_add=["PhysX Static Rigid Body", "PhysX Primitive Collider"],
    )
    set_scale(ground, (1200.0, 900.0, 1.0))
    set_mesh(ground, BOX, material_paths("grass"))
    add_policy(ground)
    return ground


def add_car(index, points, manager, camera, project_name):
    name = "Car 0 (Player)" if index == 0 else f"Car {index}"
    player = index == 0
    grid = ((16.0, -3.0), (16.0, 3.0), (24.0, -3.0), (24.0, 3.0))[index]
    position = point_at_distance_back(points, 0, grid[0])
    tx, ty = tangent(points, 0)
    normal = (-ty, tx)
    position = (
        position[0] + normal[0] * grid[1],
        position[1] + normal[1] * grid[1],
        0.85,
    )
    component_names = ["PhysX Dynamic Rigid Body", "PhysX Primitive Collider"]
    if player:
        component_names.extend(["Input", "World Partition Streaming Source"])
    else:
        component_names.append("Behavior Tree")
    car = make_entity(name, position, parent=always_loaded_parent_id, components_to_add=component_names)
    set_rotation(car, (0.0, 0.0, pymath.atan2(-tx, ty)))
    set_scale(car, (1.9, 4.2, 0.7))
    car_material = ("white", "red", "yellow", "blue")[index]
    set_mesh(car, BOX, material_paths(car_material))

    rigid_body = car["named_components"]["PhysX Dynamic Rigid Body"]
    set_property(rigid_body, ["Compute Mass"], False)
    set_property(rigid_body, ["Mass"], 1200.0)
    set_property(rigid_body, ["Compute Center of Mass"], False)
    set_property(rigid_body, ["Center of Mass Offset"], math.Vector3(0.0, 0.0, -0.35))
    add_policy(car)

    for wheel_index, (x, y) in enumerate(
        [(-0.8, -1.5), (0.8, -1.5), (-0.8, 1.5), (0.8, 1.5)], 1
    ):
        wheel = make_entity(f"{name} Wheel {wheel_index}", (x, y, -0.35), car["id"])
        set_scale(wheel, (0.46, 0.7, 0.46))
        set_mesh(wheel, SPHERE, material_paths("black"))

    arcade = add_lua(car, "ArcadeCar.lua", ["UsePlayerInput", "CarIndex", "GridLane"])
    set_lua_property(arcade, "UsePlayerInput", player)
    set_lua_property(arcade, "CarIndex", float(index))
    set_lua_property(arcade, "GridLane", float(grid[1]))
    tracker = add_lua(car, "LapTracker.lua", ["CarIndex", "Laps", "RaceManager"])
    set_lua_property(tracker, "CarIndex", float(index))
    set_lua_property(tracker, "Laps", 3.0)
    set_lua_reference(tracker, "RaceManager", manager)

    if player:
        set_player_input(car)
        remappable_input = component(car["id"], "Remappable Input")
        car["components"].append(remappable_input)
        car["named_components"]["Remappable Input"] = remappable_input
        add_lua(car, "RacingHud.lua")
        add_lua(car, "SettingsMenu.lua", ["CameraEntity"], True)
    else:
        tree = car["named_components"]["Behavior Tree"]
        set_property(
            tree,
            ["Tree file", "TreeFile"],
            "Scripts/Racing/RacingAi.behaviortree.json",
            required=True,
        )
        ai = add_lua(
            car,
            "AiDriver.lua",
            [
                "SkillSpeedScale",
                "LaneOffset",
                "LookAheadMin",
                "LookAheadPerSpeed",
                "CarIndex",
            ],
        )
        set_lua_property(ai, "SkillSpeedScale", (0.88, 0.93, 0.97)[index - 1])
        set_lua_property(ai, "LaneOffset", (-3.0, 0.0, 3.0)[index - 1])
        set_lua_property(ai, "LookAheadMin", 8.0)
        set_lua_property(ai, "LookAheadPerSpeed", 0.6)
        set_lua_property(ai, "CarIndex", float(index))
    return car


def add_arch(index, group, points, anchor, project_name):
    global trophy_entity
    point = points[index]
    tx, ty = tangent(points, index)
    normal = (-ty, tx)
    yaw = pymath.atan2(-tx, ty)
    for side, label in [(-1.0, "Left"), (1.0, "Right")]:
        post_position = (
            point[0] + normal[0] * side * 8.5,
            point[1] + normal[1] * side * 8.5,
            1.75,
        )
        add_visual_box(
            f"{group['name']} Checkpoint {label}",
            post_position,
            (0.55, 0.55, 3.5),
            group["id"],
            anchor,
            "white",
            yaw,
        )
    add_visual_box(
        f"{group['name']} Checkpoint Top",
        (point[0], point[1], 3.5),
        (18.0, 0.5, 0.55),
        group["id"],
        anchor,
        "red" if index == 0 else "blue",
        yaw,
    )
    if index == 0:
        for tile in range(14):
            offset = tile - 6.5
            tile_position = (
                point[0] + normal[0] * offset,
                point[1] + normal[1] * offset,
                4.0,
            )
            add_visual_box(
                f"Start Finish Checker {tile + 1}",
                tile_position,
                (1.0, 0.16, 0.7),
                group["id"],
                anchor,
                "white" if tile % 2 else "black",
                yaw,
            )
        trophy_position = (
            point[0] + normal[0] * 11.0,
            point[1] + normal[1] * 11.0,
            1.1,
        )
        trophy = add_visual_mesh(
            "Circuit Trophy",
            trophy_position,
            (1.2, 1.2, 1.6),
            group["id"],
            anchor,
            SPHERE,
            "gold",
        )
        spinner = component(trophy["id"], f"{project_name} Spinner")
        trophy["components"].append(spinner)
        trophy["named_components"]["Spinner"] = spinner
        set_property(spinner, ["Degrees per second"], 90.0)
        trophy_entity = trophy


def curvature(points, index):
    before = tangent(points, (index - 1) % len(points))
    after = tangent(points, index)
    dot = max(-1.0, min(1.0, before[0] * after[0] + before[1] * after[1]))
    cross = before[0] * after[1] - before[1] * after[0]
    return abs(pymath.atan2(cross, dot))


def add_sector_trees(sector, group, points, start, end, anchor):
    rng = random.Random(8417 + sector)
    count = 12
    span = max(1, end - start)
    for item in range(count):
        index = (start + int((item + 0.5) * span / count)) % len(points)
        point = points[index]
        tx, ty = tangent(points, index)
        side = -1.0 if item % 2 == 0 else 1.0
        offset = rng.uniform(30.0, 58.0)
        height = rng.uniform(4.0, 7.0)
        x = point[0] - ty * side * offset
        y = point[1] + tx * side * offset
        add_visual_box(
            f"Sector {sector} Tree {item + 1} Trunk",
            (x, y, height * 0.25),
            (0.65, 0.65, height * 0.5),
            group["id"],
            anchor,
            "bark",
        )
        add_visual_mesh(
            f"Sector {sector} Tree {item + 1} Leaves",
            (x, y, height * 0.75),
            (height * 0.75, height * 0.75, height * 0.75),
            group["id"],
            anchor,
            SPHERE,
            "leaves",
        )


def add_tire_stacks(points, sector_groups, anchors):
    count = len(points)
    hairpin = max(range(count), key=lambda index: curvature(points, index))
    for pile in range(4):
        index = (hairpin + pile * 2 - 3) % count
        sector = min(7, index * 8 // count)
        group = sector_groups[sector]
        anchor = anchors[sector]
        point = points[index]
        tx, ty = tangent(points, index)
        side = -1.0 if pile % 2 == 0 else 1.0
        center_x = point[0] - ty * side * 10.0
        center_y = point[1] + tx * side * 10.0
        for tire in range(4):
            add_visual_mesh(
                f"Hairpin Tire Stack {pile + 1} Tire {tire + 1}",
                (center_x, center_y, 0.35 + tire * 0.52),
                (0.9, 0.9, 0.52),
                group["id"],
                anchor,
                CYLINDER,
                "black",
            )


def add_track_sectors(points, project_name):
    count = len(points)
    sector_groups = []
    anchors = []
    ranges = []
    for sector in range(8):
        start = sector * count // 8
        end = (sector + 1) * count // 8
        anchor = points[start]
        group = add_group(f"Sector {sector + 1}", anchor)
        sector_groups.append(group)
        anchors.append(anchor)
        ranges.append((start, end))

    for sector, (start, end) in enumerate(ranges):
        group = sector_groups[sector]
        anchor = anchors[sector]
        for index in range(start, end):
            next_index = (index + 1) % count
            point = points[index]
            next_point = points[next_index]
            dx = next_point[0] - point[0]
            dy = next_point[1] - point[1]
            length = pymath.hypot(dx, dy) + 0.3
            yaw = pymath.atan2(-dx, dy)
            midpoint = (
                (point[0] + next_point[0]) * 0.5,
                (point[1] + next_point[1]) * 0.5,
                -0.03,
            )
            add_visual_box(
                f"Sector {sector + 1} Road {index:03d}",
                midpoint,
                (TRACK_WIDTH, length, 0.1),
                group["id"],
                anchor,
                "asphalt",
                yaw,
            )
            if curvature(points, index) >= 0.055 and index % 2 == 0:
                for side, label in [(-1.0, "Left"), (1.0, "Right")]:
                    normal = (-dy / pymath.hypot(dx, dy), dx / pymath.hypot(dx, dy))
                    curb_position = (
                        midpoint[0] + normal[0] * side * (TRACK_WIDTH * 0.5 - 0.55),
                        midpoint[1] + normal[1] * side * (TRACK_WIDTH * 0.5 - 0.55),
                        0.08,
                    )
                    add_visual_box(
                        f"Sector {sector + 1} {label} Kerb {index:03d}",
                        curb_position,
                        (1.1, length, 0.14),
                        group["id"],
                        anchor,
                        "red" if (index // 2) % 2 == 0 else "white",
                        yaw,
                    )
        add_arch(start, group, points, anchor, project_name)
        add_sector_trees(sector + 1, group, points, start, end, anchor)
    add_tire_stacks(points, sector_groups, anchors)
    return sector_groups


def clear_level_entities():
    roots = editor.EditorEntityInfoRequestBus(
        bus.Event, "GetChildren", level_root_entity_id
    )
    names = [
        editor.EditorEntityInfoRequestBus(bus.Event, "GetName", entity_id)
        for entity_id in roots
    ]
    print(f"Circuit level root has {len(roots)} children: {names}")
    if roots:
        editor.ToolsApplicationRequestBus(
            bus.Broadcast, "DeleteEntitiesAndAllDescendants", roots
        )
    general.idle_wait_frames(2)
    remaining = editor.EditorEntityInfoRequestBus(
        bus.Event, "GetChildren", level_root_entity_id
    )
    if remaining:
        raise RuntimeError(f"Could not clear {len(remaining)} existing Circuit roots")


def postprocess_prefab():
    prefab_path = os.path.abspath(
        os.path.join(
            os.path.dirname(__file__),
            "..",
            "..",
            "Levels",
            "Circuit",
            "Circuit.prefab",
        )
    )
    with open(prefab_path, "r", encoding="utf-8") as handle:
        prefab = json.load(handle)
    container_id = prefab.get("ContainerEntity", {}).get("Id")
    entities_by_name = {
        item.get("Name"): item
        for item in prefab.get("Entities", {}).values()
        if item.get("Name")
    }
    if not container_id:
        raise RuntimeError("Circuit prefab has no container entity")
    player = entities_by_name.get("Car 0 (Player)")
    remappable_input = None
    if player:
        def find_remappable(value):
            if isinstance(value, dict):
                if "RemappableInputComponent" in value.get("$type", ""):
                    return value
                for nested in value.values():
                    found = find_remappable(nested)
                    if found is not None:
                        return found
            elif isinstance(value, list):
                for nested in value:
                    found = find_remappable(nested)
                    if found is not None:
                        return found
            return None

        remappable_input = find_remappable(player.get("Components", {}))
    if remappable_input is None:
        raise RuntimeError("Player car has no serialized Remappable Input component")
    remappable_input["Bindings"] = [
        {
            "EventName": "ResetCar",
            "BindingKey": "bind_jump",
            "DefaultChannel": "keyboard_key_edit_space",
            "ScaleSetting": "",
            "ScaleDefault": 1.0,
        },
        {
            "EventName": "ResetCar",
            "BindingKey": "",
            "DefaultChannel": "gamepad_button_a",
            "ScaleSetting": "",
            "ScaleDefault": 1.0,
        },
    ]

    parent_updates = 0
    for record in parent_records:
        entity_data = entities_by_name.get(record["name"])
        if entity_data is None:
            raise RuntimeError(f"Missing serialized entity {record['name']}")
        transform = next(
            (
                item
                for item in entity_data["Components"].values()
                if "TransformComponent" in item.get("$type", "")
            ),
            None,
        )
        if transform is None or "Parent Entity" not in transform:
            raise RuntimeError(f"{record['name']} has no serialized Transform parent")
        parent_name = entity_names_by_id.get(str(record["parent_id"]))
        if parent_name in ("Always Loaded Staging", "Streamed Staging"):
            parent_id = container_id
        else:
            parent_id = (
                entities_by_name[parent_name]["Id"] if parent_name else container_id
            )
        if transform["Parent Entity"] != parent_id:
            transform["Parent Entity"] = parent_id
            parent_updates += 1

    def script_for(entity_name, asset_hint):
        owner = entities_by_name.get(entity_name)
        matches = [
            item
            for item in owner.get("Components", {}).values()
            if item.get("$type") == "ScriptEditorComponent"
            and item.get("ScriptAsset", {}).get("assetHint", "").lower()
            == asset_hint.lower()
        ] if owner else []
        if len(matches) != 1:
            raise RuntimeError(
                f"Expected one {asset_hint} script on {entity_name}; found {len(matches)}"
            )
        return matches[0]

    for source_name, asset_hint, name, target_name in pending_references:
        script = script_for(source_name, asset_hint)
        properties = (
            script.get("ScriptComponent", {})
            .get("Properties", {})
            .get("Properties", [])
        )
        entry = next((item for item in properties if item.get("name") == name), None)
        if (
            entry is None
            or not isinstance(entry.get("value"), dict)
            or "value" not in entry["value"]
        ):
            raise RuntimeError(f"Editor did not serialize {source_name}.{name}")
        target = entities_by_name.get(target_name)
        if target is None:
            raise RuntimeError(f"Missing serialized reference target {target_name}")
        entry["value"]["value"] = target["Id"]

    for source_name, asset_hint, name, vector_value in pending_vector_properties:
        script = script_for(source_name, asset_hint)
        properties = (
            script.get("ScriptComponent", {})
            .get("Properties", {})
            .get("Properties", [])
        )
        entry = next((item for item in properties if item.get("name") == name), None)
        if (
            entry is None
            or not isinstance(entry.get("value"), dict)
            or "value" not in entry["value"]
        ):
            raise RuntimeError(f"Editor did not serialize {source_name}.{name}")
        entry["value"]["value"] = list(vector_value)

    staging_ids = {
        entities_by_name[name]["Id"]
        for name in ("Always Loaded Staging", "Streamed Staging")
        if name in entities_by_name
    }
    entities = prefab.get("Entities", {})
    for key, entity_data in list(entities.items()):
        if entity_data.get("Id") in staging_ids:
            del entities[key]

    direct_child_ids = []
    for entity_data in entities.values():
        for component_data in entity_data.get("Components", {}).values():
            if "TransformComponent" in component_data.get("$type", ""):
                if component_data.get("Parent Entity") == container_id:
                    direct_child_ids.append(entity_data["Id"])
                break
    sort_component = next(
        (
            item
            for item in prefab.get("ContainerEntity", {}).get("Components", {}).values()
            if item.get("$type") == "EditorEntitySortComponent"
            and "Child Entity Order" in item
        ),
        None,
    )
    if sort_component is None:
        raise RuntimeError("Circuit container has no serialized child order")
    sort_component["Child Entity Order"] = direct_child_ids
    temporary_path = prefab_path + ".tmp"
    with open(temporary_path, "w", encoding="utf-8") as handle:
        json.dump(prefab, handle, indent=4)
        handle.write("\n")
    os.replace(temporary_path, prefab_path)
    print(
        f"Updated {parent_updates} parents and {len(pending_references)} Lua references; "
        f"removed {len(staging_ids)} staging entities from {prefab_path}"
    )


def save_level():
    path = os.path.abspath(
        os.path.join(
            os.path.dirname(__file__),
            "..",
            "..",
            "Levels",
            "Circuit",
            "Circuit.prefab",
        )
    )
    previous = os.stat(path).st_mtime_ns if os.path.exists(path) else 0
    if not general.save_level():
        raise RuntimeError(f"Editor failed to save Circuit at {path}")
    deadline = time.monotonic() + 20.0
    while time.monotonic() < deadline:
        general.idle_wait(0.1)
        if os.path.exists(path) and os.stat(path).st_mtime_ns > previous:
            return
    raise RuntimeError(f"Timed out waiting for Circuit prefab at {path}")


def main():
    global always_loaded_parent_id, level_root_entity_id, streamed_parent_id
    global entity_count
    general.idle_enable(True)
    project_root = os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", "..")
    )
    with open(os.path.join(project_root, "project.json"), "r", encoding="utf-8") as handle:
        project_name = json.load(handle)["project_name"]
    print("Generating Racing Circuit and shared track data")
    points, length = generate_track()
    write_track_data(points)
    print(f"Circuit length {length:.2f} m, samples {len(points)}")

    create_result = general.create_level_no_prompt(
        "DefaultLevel", "Circuit", 1024, 1, 4096, False
    )
    print(f"create_level_no_prompt returned {create_result}")
    if not general.open_level_no_prompt("Circuit"):
        raise RuntimeError("Could not open Circuit")
    general.idle_wait_frames(60)
    level_root_entity_id = editor.ToolsApplicationRequestBus(
        bus.Broadcast, "GetCurrentLevelEntityId"
    )
    if not level_root_entity_id.IsValid():
        raise RuntimeError("Could not get Circuit level root")
    clear_level_entities()

    always_loaded = make_entity("Always Loaded Staging", parent=level_root_entity_id)
    streamed = make_entity("Streamed Staging", parent=level_root_entity_id)
    always_loaded_parent_id = always_loaded["id"]
    streamed_parent_id = streamed["id"]

    world = make_entity(
        "World",
        parent=always_loaded_parent_id,
        components_to_add=["World Partition"],
    )
    partition = world["components"][0]
    for names, value in [
        (["Cell size"], 64.0),
        (["Load radius"], 160.0),
        (["Unload radius"], 200.0),
        (["2D grid"], True),
        (["Stream by default"], True),
    ]:
        set_property(partition, names, value, required=True)
    add_policy(world)

    sun = make_entity(
        "Sun",
        parent=always_loaded_parent_id,
        components_to_add=["Directional Light"],
    )
    set_rotation(sun, (-0.8726646, 0.0, 0.5235988))
    set_property(sun["components"][0], ["Enable Shadow"], True, required=True)
    add_policy(sun)

    skybox = wait_for_asset(
        ["lightingpresets/highcontrast/goegap_4k_skyboxcm.exr.streamingimage"]
    )
    diffuse = wait_for_asset(
        ["lightingpresets/highcontrast/goegap_4k_iblglobalcm_ibldiffuse.exr.streamingimage"]
    )
    specular = wait_for_asset(
        ["lightingpresets/highcontrast/goegap_4k_iblglobalcm_iblspecular.exr.streamingimage"]
    )
    sky = make_entity(
        "Sky",
        parent=always_loaded_parent_id,
        components_to_add=["HDRi Skybox", "Global Skylight (IBL)"],
    )
    set_property(sky["components"][0], ["Cubemap Texture", "CubemapAsset"], skybox, True)
    set_property(
        sky["components"][1], ["Diffuse Image", "diffuseImageAsset"], diffuse, True
    )
    set_property(
        sky["components"][1], ["Specular Image", "specularImageAsset"], specular, True
    )
    add_policy(sky)

    add_ground((0.0, 0.0, 0.0))
    manager = make_entity("Race", parent=always_loaded_parent_id)
    add_policy(manager)
    cars = [
        add_car(index, points, manager, None, project_name) for index in range(4)
    ]
    camera = make_entity(
        "Chase Camera",
        parent=always_loaded_parent_id,
        components_to_add=["Camera"],
    )
    add_policy(camera)
    camera_script = add_lua(
        camera,
        "ChaseCamera.lua",
        ["Target", "Distance", "Height", "PositionSmoothing", "LookSmoothing"],
    )
    set_lua_reference(camera_script, "Target", cars[0])
    for name, value in [
        ("Distance", 8.0),
        ("Height", 3.0),
        ("PositionSmoothing", 6.0),
        ("LookSmoothing", 10.0),
    ]:
        set_lua_property(camera_script, name, value)
    settings_script = next(
        record
        for record in script_records
        if record["entity"] == "Car 0 (Player)"
        and record["asset_hint"].endswith("settingsmenu.luac")
    )
    set_lua_reference(settings_script, "CameraEntity", camera)

    race_script = add_lua(
        manager,
        "RaceManager.lua",
        ["Car0", "Car1", "Car2", "Car3", "CountdownSeconds"],
    )
    set_lua_property(race_script, "CountdownSeconds", 3.0)
    for index, car in enumerate(cars):
        set_lua_reference(race_script, f"Car{index}", car)

    add_track_sectors(points, project_name)
    save_level()
    postprocess_prefab()
    print(
        f"Generated Circuit: {len(points)} points, 8 streamed sectors, "
        "four cars, three-lap race"
    )


try:
    main()
except Exception as error:
    print(f"Circuit generation failed: {error}")
    raise
finally:
    general.exit_no_prompt()
