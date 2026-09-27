"""
Copyright (c) Contributors to the Open 3D Engine Project.
For complete copyright and license terms please see the LICENSE at the root of this distribution.

SPDX-License-Identifier: Apache-2.0 OR MIT
"""
# Unit tests for llmassist.scene_actions using a fake backend (no Editor).
# Run: python -m unittest discover -s Gems/LLMAssist/Tests

import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "Editor", "Scripts"))

from llmassist import scene_actions  # noqa: E402
from llmassist.scene_actions import ActionError, Executor  # noqa: E402


class FakeBackend:
    """In-memory level: entities by id, components as {(entity, type): {path: value}}."""

    TYPE_NAMES = ["Mesh", "Tag", "PhysX Dynamic Rigid Body", "PhysX Static Rigid Body",
                  "Script", "C# Script", "Box Shape"]
    PROPERTIES = {
        "PhysX Dynamic Rigid Body": ["Configuration|Mass", "Configuration|Linear damping",
                                     "Configuration|Gravity enabled"],
        "Mesh": ["Controller|Configuration|Mesh Asset"],
        "Tag": ["Tags"],
        "Script": ["Script asset", "Properties|Speed"],
        "C# Script": ["Class name"],
        "Box Shape": ["Box Shape|Box Configuration|Dimensions"],
        "PhysX Static Rigid Body": [],
    }

    def __init__(self):
        self.entities = {}  # id -> dict(name, parent)
        self.components = {}  # component id -> dict(entity, type, values)
        self.selected = []
        self.console = []
        self.saved = 0
        self.undo = []
        self._next = 1

    # helpers for tests
    def add_entity(self, name, parent=None):
        entity_id = f"[{self._next}]"
        self._next += 1
        self.entities[entity_id] = {"name": name, "parent": parent}
        return entity_id

    def add_fake_component(self, entity_id, type_name, values=None):
        component = f"c{self._next}"
        self._next += 1
        self.components[component] = {"entity": entity_id, "type": type_name, "values": dict(values or {})}
        return component

    # backend protocol
    def list_entities(self):
        return [(e, d["name"], d["parent"]) for e, d in self.entities.items()]

    def entity_id_string(self, entity_id):
        return entity_id or ""

    def entity_name(self, entity_id):
        return self.entities[entity_id]["name"]

    def entity_exists(self, entity_id):
        return entity_id in self.entities

    def find_entity_by_id_string(self, text):
        return text if text in self.entities else None

    def find_entities_by_name(self, name):
        return [e for e, d in self.entities.items() if d["name"].lower() == name.lower()]

    def selected_entities(self):
        return list(self.selected)

    def select_entities(self, ids):
        self.selected = list(ids)

    def create_entity(self, name, parent_id, position):
        entity_id = self.add_entity(name, parent_id)
        self.entities[entity_id]["position"] = position
        return entity_id

    def delete_entity(self, entity_id):
        for child in [e for e, d in self.entities.items() if d["parent"] == entity_id]:
            self.delete_entity(child)
        del self.entities[entity_id]
        for component in [c for c, d in self.components.items() if d["entity"] == entity_id]:
            del self.components[component]

    def rename_entity(self, entity_id, name):
        self.entities[entity_id]["name"] = name

    def set_parent(self, entity_id, parent_id):
        self.entities[entity_id]["parent"] = parent_id

    def set_world_translation(self, entity_id, xyz):
        self.entities[entity_id]["position"] = xyz

    def set_local_rotation_degrees(self, entity_id, xyz):
        self.entities[entity_id]["rotation"] = xyz

    def set_local_uniform_scale(self, entity_id, scale):
        self.entities[entity_id]["scale"] = scale

    def component_type_names(self):
        return list(self.TYPE_NAMES)

    def entity_components(self, entity_id):
        return [(d["type"], c) for c, d in self.components.items() if d["entity"] == entity_id]

    def get_component(self, entity_id, type_name):
        for c, d in self.components.items():
            if d["entity"] == entity_id and d["type"] == type_name:
                return c
        return None

    def add_component(self, entity_id, type_name):
        if type_name == "PhysX Static Rigid Body" and self.get_component(entity_id, "PhysX Dynamic Rigid Body"):
            raise ActionError("incompatible")
        return self.add_fake_component(entity_id, type_name)

    def remove_component(self, component):
        del self.components[component]

    def component_property_paths(self, component):
        return list(self.PROPERTIES[self.components[component]["type"]])

    def get_property_text(self, component, path):
        return str(self.components[component]["values"].get(path, "?"))

    def set_property(self, component, path, value, resolve_entity):
        if isinstance(value, str) and value.startswith("entity:"):
            value = resolve_entity(value[len("entity:"):])
        self.components[component]["values"][path] = value
        return path != "Configuration|Gravity enabled" or isinstance(value, bool)

    def run_console(self, command):
        self.console.append(command)

    def save_level(self):
        self.saved += 1

    def begin_undo_batch(self, label):
        self.undo.append(("begin", label))

    def end_undo_batch(self):
        self.undo.append(("end", None))


def _reply(actions_json):
    return f"Sure, here you go:\n\n```actions\n{actions_json}\n```\n"


class ParseTests(unittest.TestCase):
    def test_no_block(self):
        self.assertFalse(scene_actions.has_actions("just text"))
        self.assertEqual(scene_actions.parse_actions("just text"), [])
        self.assertEqual(scene_actions.parse_actions(None), [])

    def test_parses_list_and_single_object(self):
        actions = scene_actions.parse_actions(_reply('[{"action": "save_level"}, {"action": "rebuild_csharp"}]'))
        self.assertEqual([a["action"] for a in actions], ["save_level", "rebuild_csharp"])
        actions = scene_actions.parse_actions(_reply('{"action": "rebuild_csharp"}'))
        self.assertEqual(len(actions), 1)

    def test_multiple_blocks_in_order(self):
        reply = _reply('[{"action": "rebuild_csharp"}]') + _reply('[{"action": "save_level"}]')
        self.assertEqual([a["action"] for a in scene_actions.parse_actions(reply)],
                         ["rebuild_csharp", "save_level"])

    def test_language_tag_suffix_and_empty_block(self):
        self.assertEqual(scene_actions.parse_actions("```actions json\n[]\n```"), [])
        self.assertEqual(scene_actions.parse_actions("```actions\n\n```"), [])

    def test_rejects_bad_json_unknown_action_and_missing_name(self):
        with self.assertRaises(ActionError):
            scene_actions.parse_actions(_reply("[{oops"))
        with self.assertRaises(ActionError):
            scene_actions.parse_actions(_reply('[{"action": "format_disk"}]'))
        with self.assertRaises(ActionError):
            scene_actions.parse_actions(_reply('[{"entity": "x"}]'))
        with self.assertRaises(ActionError):
            scene_actions.parse_actions(_reply('"a string"'))

    def test_file_block_is_not_an_actions_block(self):
        self.assertFalse(scene_actions.has_actions("FILE: a.cs\n```csharp\nclass A {}\n```"))

    def test_destructive_flags(self):
        for kind in ("delete_entity", "remove_component", "save_level", "run_console"):
            self.assertTrue(scene_actions.is_destructive({"action": kind}))
        for kind in ("create_entity", "select", "set_property", "add_component", "rebuild_csharp"):
            self.assertFalse(scene_actions.is_destructive({"action": kind}))

    def test_describe_covers_every_action(self):
        for kind in scene_actions.SUPPORTED_ACTIONS:
            text = scene_actions.describe({"action": kind, "entity": "E", "name": "N",
                                           "component": "C", "property": "P", "value": 1})
            self.assertTrue(text and text != "{}")


class ExecutorTests(unittest.TestCase):
    def setUp(self):
        self.backend = FakeBackend()
        self.props = self.backend.add_entity("Props")
        self.player = self.backend.add_entity("Player")
        self.body = self.backend.add_fake_component(
            self.player, "PhysX Dynamic Rigid Body",
            {"Configuration|Mass": 80.0, "Configuration|Gravity enabled": True})
        self.confirmed = []

    def run_actions(self, actions, confirm=None):
        executor = Executor(self.backend, confirm=confirm)
        return executor.run(actions)

    def test_create_add_set_select_in_one_batch(self):
        applied, messages = self.run_actions([
            {"action": "create_entity", "name": "Crate", "parent": "Props", "position": [1, 2, 3]},
            {"action": "add_component", "entity": "Crate", "component": "physx dynamic rigid body"},
            {"action": "set_property", "entity": "Crate", "component": "Dynamic Rigid",
             "property": "Mass", "value": 25},
            {"action": "set_transform", "entity": "crate", "rotation_degrees": [0, 0, 45], "scale": 2},
            {"action": "select", "entities": ["Crate", "Player"]},
        ])
        self.assertEqual(applied, 5, messages)
        crate = self.backend.find_entities_by_name("Crate")[0]
        self.assertEqual(self.backend.entities[crate]["parent"], self.props)
        self.assertEqual(self.backend.entities[crate]["position"], (1.0, 2.0, 3.0))
        self.assertEqual(self.backend.entities[crate]["rotation"], (0.0, 0.0, 45.0))
        self.assertEqual(self.backend.entities[crate]["scale"], 2.0)
        component = self.backend.get_component(crate, "PhysX Dynamic Rigid Body")
        self.assertEqual(self.backend.components[component]["values"]["Configuration|Mass"], 25)
        self.assertEqual(self.backend.selected, [crate, self.player])
        self.assertEqual(self.backend.undo, [("begin", "AI Assistant actions"), ("end", None)])

    def test_reference_by_id_string_and_rename(self):
        applied, messages = self.run_actions([
            {"action": "rename_entity", "entity": self.player, "name": "Hero"},
            {"action": "set_parent", "entity": "Hero", "parent": "Props"},
            {"action": "set_parent", "entity": "Hero", "parent": None},
        ])
        self.assertEqual(applied, 3, messages)
        self.assertEqual(self.backend.entities[self.player]["name"], "Hero")
        self.assertIsNone(self.backend.entities[self.player]["parent"])

    def test_ambiguous_and_missing_entities_fail_and_stop(self):
        self.backend.add_entity("Player")
        applied, messages = self.run_actions([
            {"action": "select", "entities": ["Player"]},
            {"action": "rebuild_csharp"},
        ])
        self.assertEqual(applied, 0)
        self.assertIn("2 entities are named", messages[0])
        self.assertEqual(self.backend.console, [])  # stopped after the failure

        applied, messages = self.run_actions([{"action": "select", "entities": ["Ghost"]}])
        self.assertEqual(applied, 0)
        self.assertIn("no entity named 'Ghost'", messages[0])

    def test_component_resolution(self):
        executor = Executor(self.backend)
        self.assertEqual(executor.resolve_component_type("mesh"), "Mesh")
        self.assertEqual(executor.resolve_component_type("c#script"), "C# Script")
        self.assertEqual(executor.resolve_component_type("Static Rigid"), "PhysX Static Rigid Body")
        # "Script" matches "Script" exactly even though "C# Script" contains it.
        self.assertEqual(executor.resolve_component_type("Script"), "Script")
        with self.assertRaises(ActionError):
            executor.resolve_component_type("Rigid Body")  # dynamic or static?
        with self.assertRaises(ActionError):
            executor.resolve_component_type("Teleporter")
        with self.assertRaises(ActionError):
            executor.resolve_component_type("")

    def test_property_resolution(self):
        executor = Executor(self.backend)
        self.assertEqual(executor.resolve_property(self.body, "Configuration|Mass"), "Configuration|Mass")
        self.assertEqual(executor.resolve_property(self.body, "mass"), "Configuration|Mass")
        self.assertEqual(executor.resolve_property(self.body, "linear_damping"), "Configuration|Linear damping")
        self.assertEqual(executor.resolve_property(self.body, "Gravity Enabled"), "Configuration|Gravity enabled")
        with self.assertRaises(ActionError) as ctx:
            executor.resolve_property(self.body, "Friction")
        self.assertIn("available:", str(ctx.exception))

    def test_set_property_rejected_by_editor_reports_failure(self):
        applied, messages = self.run_actions([
            {"action": "set_property", "entity": "Player", "component": "PhysX Dynamic Rigid Body",
             "property": "Gravity enabled", "value": "maybe"},
        ])
        self.assertEqual(applied, 0)
        self.assertIn("rejected", messages[0])

    def test_set_property_missing_component_or_value(self):
        applied, messages = self.run_actions([
            {"action": "set_property", "entity": "Props", "component": "Mesh", "property": "Mesh Asset", "value": 1},
        ])
        self.assertEqual(applied, 0)
        self.assertIn("has no 'Mesh' component", messages[0])
        applied, messages = self.run_actions([
            {"action": "set_property", "entity": "Player", "component": "PhysX Dynamic Rigid Body",
             "property": "Mass"},
        ])
        self.assertEqual(applied, 0)
        self.assertIn('needs a "value"', messages[0])

    def test_entity_reference_value_resolves_created_entity(self):
        applied, messages = self.run_actions([
            {"action": "create_entity", "name": "Target"},
            {"action": "add_component", "entity": "Player", "component": "Script"},
            {"action": "set_property", "entity": "Player", "component": "Script",
             "property": "Speed", "value": "entity:Target"},
        ])
        self.assertEqual(applied, 3, messages)
        target = self.backend.find_entities_by_name("Target")[0]
        script = self.backend.get_component(self.player, "Script")
        self.assertEqual(self.backend.components[script]["values"]["Properties|Speed"], target)

    def test_destructive_actions_need_confirmation(self):
        asked = []

        def confirm(action, text):
            asked.append(action["action"])
            return action["action"] != "delete_entity"

        applied, messages = self.run_actions([
            {"action": "delete_entity", "entity": "Props"},
            {"action": "remove_component", "entity": "Player", "component": "PhysX Dynamic Rigid Body"},
            {"action": "run_console", "command": "r_displayInfo 1"},
            {"action": "save_level"},
            {"action": "rebuild_csharp"},
        ], confirm=confirm)
        self.assertEqual(asked, ["delete_entity", "remove_component", "run_console", "save_level"])
        self.assertEqual(applied, 4)
        self.assertIn(self.props, self.backend.entities)  # declined
        self.assertIsNone(self.backend.get_component(self.player, "PhysX Dynamic Rigid Body"))
        self.assertEqual(self.backend.console, ["r_displayInfo 1", "csharp_rebuild"])
        self.assertEqual(self.backend.saved, 1)
        self.assertTrue(any("skipped (user declined)" in m for m in messages))

    def test_delete_entity_removes_children_and_created_alias(self):
        applied, messages = self.run_actions([
            {"action": "create_entity", "name": "Temp", "parent": "Props"},
            {"action": "delete_entity", "entity": "Props"},
            {"action": "select", "entities": ["Temp"]},
        ])
        self.assertEqual(applied, 2)
        self.assertIn("no entity named 'Temp'", messages[2])
        self.assertNotIn(self.props, self.backend.entities)

    def test_add_component_failure_is_reported(self):
        applied, messages = self.run_actions([
            {"action": "add_component", "entity": "Player", "component": "PhysX Static Rigid Body"},
        ])
        self.assertEqual(applied, 0)
        self.assertIn("incompatible", messages[0])

    def test_transform_validation(self):
        for bad in (
            {"action": "set_transform", "entity": "Player"},
            {"action": "set_transform", "entity": "Player", "position": [1, 2]},
            {"action": "set_transform", "entity": "Player", "position": ["a", "b", "c"]},
            {"action": "set_transform", "entity": "Player", "scale": -1},
            {"action": "set_transform", "entity": "Player", "scale": [1, 2, 3]},
            {"action": "set_transform", "entity": "Player", "scale": "big"},
        ):
            applied, messages = self.run_actions([bad])
            self.assertEqual(applied, 0, bad)
        applied, _ = self.run_actions([{"action": "set_transform", "entity": "Player", "scale": [2, 2, 2]}])
        self.assertEqual(applied, 1)
        self.assertEqual(self.backend.entities[self.player]["scale"], 2.0)

    def test_self_parenting_and_empty_inputs_rejected(self):
        applied, _ = self.run_actions([{"action": "set_parent", "entity": "Player", "parent": "Player"}])
        self.assertEqual(applied, 0)
        applied, _ = self.run_actions([{"action": "select", "entities": []}])
        self.assertEqual(applied, 0)
        applied, _ = self.run_actions([{"action": "run_console", "command": "  "}])
        self.assertEqual(applied, 0)
        applied, _ = self.run_actions([{"action": "rename_entity", "entity": "Player", "name": ""}])
        self.assertEqual(applied, 0)

    def test_undo_batch_closed_even_when_backend_raises_unexpectedly(self):
        def boom(*args, **kwargs):
            raise RuntimeError("editor exploded")
        self.backend.select_entities = boom
        applied, messages = self.run_actions([{"action": "select", "entities": ["Player"]}])
        self.assertEqual(applied, 0)
        self.assertIn("RuntimeError: editor exploded", messages[0])
        self.assertEqual(self.backend.undo[-1], ("end", None))


class SceneContextTests(unittest.TestCase):
    def test_lists_entities_and_selected_components(self):
        backend = FakeBackend()
        props = backend.add_entity("Props")
        crate = backend.add_entity("Crate", props)
        backend.add_fake_component(crate, "PhysX Dynamic Rigid Body", {"Configuration|Mass": 25.0})
        backend.selected = [crate]
        text = scene_actions.scene_context(backend)
        self.assertIn("Entities in the open level (2)", text)
        self.assertIn(f"- Crate {crate} (parent: Props)", text)
        self.assertIn("Selected entities", text)
        self.assertIn("PhysX Dynamic Rigid Body: Configuration|Mass=25.0", text)

    def test_empty_level_and_no_selection(self):
        backend = FakeBackend()
        self.assertEqual(scene_actions.scene_context(backend), "")
        backend.add_entity("Solo")
        self.assertIn("No entity is selected", scene_actions.scene_context(backend))

    def test_truncates_huge_levels(self):
        backend = FakeBackend()
        for i in range(400):
            backend.add_entity(f"Entity {i}")
        text = scene_actions.scene_context(backend)
        self.assertIn("more", text)
        self.assertLessEqual(len(text), scene_actions._MAX_CONTEXT_CHARS + 32)

    def test_action_docs_mention_every_action(self):
        for kind in scene_actions.SUPPORTED_ACTIONS:
            self.assertIn(kind, scene_actions.ACTION_DOCS)


if __name__ == "__main__":
    unittest.main()
