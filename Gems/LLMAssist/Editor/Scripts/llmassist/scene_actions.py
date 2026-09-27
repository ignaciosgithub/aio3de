"""
Copyright (c) Contributors to the Open 3D Engine Project.
For complete copyright and license terms please see the LICENSE at the root of this distribution.

SPDX-License-Identifier: Apache-2.0 OR MIT
"""
# Lets the assistant act on the open level instead of only proposing file
# edits. A reply may contain a fenced ```actions block holding a JSON list of
# actions (see ACTION_DOCS); the UI parses it, previews it, asks for
# confirmation and runs it through EditorBackend (azlmbr / EditorPython
# bindings) inside one undo batch so Ctrl+Z reverts the whole batch.
#
# The action vocabulary, parsing, entity/component resolution and execution
# order live here and only talk to the Editor through a small backend object,
# so everything except the azlmbr calls themselves is unit-testable with a
# fake backend (see Gems/LLMAssist/Tests/test_scene_actions.py).

import json
import re

_ACTIONS_BLOCK = re.compile(r"```actions[^\n]*\n(?P<body>.*?)```", re.DOTALL)

# Actions that change or remove existing work; each one is confirmed
# individually before it runs, on top of the whole-batch confirmation.
DESTRUCTIVE_ACTIONS = frozenset({
    "delete_entity", "remove_component", "save_level", "run_console",
})

SUPPORTED_ACTIONS = frozenset({
    "create_entity", "delete_entity", "rename_entity", "set_parent", "select",
    "set_transform", "add_component", "remove_component", "set_property",
    "rebuild_csharp", "run_console", "save_level",
})

ACTION_DOCS = """\
To change the OPEN LEVEL directly, add one fenced block to your reply:

```actions
[
  {"action": "create_entity", "name": "Crate", "parent": "Props", "position": [0, 0, 1]},
  {"action": "add_component", "entity": "Crate", "component": "Mesh"},
  {"action": "add_component", "entity": "Crate", "component": "PhysX Dynamic Rigid Body"},
  {"action": "set_property", "entity": "Crate", "component": "PhysX Dynamic Rigid Body",
   "property": "Mass", "value": 25.0},
  {"action": "set_transform", "entity": "Crate", "position": [1, 2, 0.5],
   "rotation_degrees": [0, 0, 45], "scale": 1.5},
  {"action": "rename_entity", "entity": "Crate", "name": "Heavy Crate"},
  {"action": "set_parent", "entity": "Heavy Crate", "parent": "Props"},
  {"action": "select", "entities": ["Heavy Crate"]},
  {"action": "remove_component", "entity": "Heavy Crate", "component": "Tag"},
  {"action": "delete_entity", "entity": "Old Crate"},
  {"action": "rebuild_csharp"},
  {"action": "run_console", "command": "r_displayInfo 1"},
  {"action": "save_level"}
]
```

Rules:
- "entity"/"parent" reference entities by name (case-insensitive) or by the
  "[id]" shown in the scene listing; a "parent" of "" or null means the level
  root. Entities created earlier in the same block may be referenced by name.
- "component" is the component display name as shown in the Inspector
  (e.g. "Mesh", "Tag", "Script", "C# Script", "PhysX Dynamic Rigid Body");
  partial names are accepted when unambiguous.
- "property" is a property path from the component's property list (shown for
  selected entities; a bare leaf name such as "Mass" also works). Values are
  JSON: numbers, booleans, strings; [x, y, z] for Vector3; [x, y, z, w] for
  Quaternion / Color; an asset path string (e.g. "objects/box.fbx.azmodel")
  for asset properties; an entity name for entity-reference properties.
- Actions run in order inside one undo batch (Ctrl+Z reverts them all).
  delete_entity, remove_component, save_level and run_console are destructive
  and are each confirmed with the user before running.
- Lua scripts need no rebuild: saving the .lua file is picked up by the Asset
  Processor automatically. C# scripts need "rebuild_csharp" after editing.
- Use file edits (FILE: blocks) for script source, actions for the scene.
"""


class ActionError(ValueError):
    """Malformed action block or an action that cannot be applied."""


def has_actions(reply):
    return bool(_ACTIONS_BLOCK.search(reply or ""))


def parse_actions(reply):
    """All actions from every ```actions block of a reply, in order."""
    actions = []
    for match in _ACTIONS_BLOCK.finditer(reply or ""):
        body = match.group("body").strip()
        if not body:
            continue
        try:
            data = json.loads(body)
        except ValueError as e:
            raise ActionError(f"actions block is not valid JSON: {e}") from e
        if isinstance(data, dict):
            data = [data]
        if not isinstance(data, list):
            raise ActionError("actions block must be a JSON list of objects")
        for index, action in enumerate(data):
            if not isinstance(action, dict) or not isinstance(action.get("action"), str):
                raise ActionError(f"action #{index + 1} is missing an \"action\" name")
            name = action["action"]
            if name not in SUPPORTED_ACTIONS:
                raise ActionError(
                    f"unknown action '{name}' (supported: {', '.join(sorted(SUPPORTED_ACTIONS))})")
            actions.append(action)
    return actions


def is_destructive(action):
    return action.get("action") in DESTRUCTIVE_ACTIONS


def describe(action):
    """One-line human summary shown in the confirmation preview."""
    kind = action.get("action")
    if kind == "create_entity":
        text = f"create entity '{action.get('name', 'Entity')}'"
        if action.get("parent"):
            text += f" under '{action['parent']}'"
        if action.get("position") is not None:
            text += f" at {action['position']}"
        return text
    if kind == "delete_entity":
        return f"DELETE entity '{action.get('entity')}' (and its children)"
    if kind == "rename_entity":
        return f"rename '{action.get('entity')}' to '{action.get('name')}'"
    if kind == "set_parent":
        return f"parent '{action.get('entity')}' under '{action.get('parent') or '<root>'}'"
    if kind == "select":
        return f"select {action.get('entities', [])}"
    if kind == "set_transform":
        parts = [f"{k}={action[k]}" for k in ("position", "rotation_degrees", "scale") if k in action]
        return f"set transform of '{action.get('entity')}': {', '.join(parts)}"
    if kind == "add_component":
        return f"add component '{action.get('component')}' to '{action.get('entity')}'"
    if kind == "remove_component":
        return f"REMOVE component '{action.get('component')}' from '{action.get('entity')}'"
    if kind == "set_property":
        return (f"set '{action.get('entity')}' > {action.get('component')} > "
                f"{action.get('property')} = {json.dumps(action.get('value'))}")
    if kind == "rebuild_csharp":
        return "rebuild C# scripts (csharp_rebuild)"
    if kind == "run_console":
        return f"RUN console command: {action.get('command')}"
    if kind == "save_level":
        return "SAVE the level to disk"
    return json.dumps(action)


def _normalize(name):
    return re.sub(r"[\s_]+", "", str(name or "")).lower()


class Executor:
    """Applies parsed actions through a backend. `confirm(action, text)` is
    called for each destructive action and must return True to proceed."""

    def __init__(self, backend, confirm=None, log=None):
        self._backend = backend
        self._confirm = confirm or (lambda action, text: True)
        self._log = log or (lambda text: None)
        # Entities created in this run, by normalized name, so later actions
        # can refer to them even before the scene listing is refreshed.
        self._created = {}

    # ---- resolution ----

    def resolve_entity(self, ref):
        if ref is None or ref == "":
            raise ActionError("an entity reference is required")
        ref = str(ref).strip()
        created = self._created.get(_normalize(ref))
        if created is not None:
            if self._backend.entity_exists(created):
                return created
            del self._created[_normalize(ref)]
        entity_id = self._backend.find_entity_by_id_string(ref)
        if entity_id is not None:
            return entity_id
        matches = self._backend.find_entities_by_name(ref)
        if len(matches) == 1:
            return matches[0]
        if not matches:
            raise ActionError(f"no entity named '{ref}' in the open level")
        raise ActionError(
            f"{len(matches)} entities are named '{ref}'; use the [id] shown in the scene listing")

    def resolve_parent(self, ref):
        if ref is None or str(ref).strip() == "":
            return None
        return self.resolve_entity(ref)

    def resolve_component_type(self, name):
        if not name:
            raise ActionError("a component name is required")
        wanted = _normalize(name)
        names = self._backend.component_type_names()
        exact = [n for n in names if _normalize(n) == wanted]
        if len(exact) == 1:
            return exact[0]
        partial = [n for n in names if wanted in _normalize(n)]
        if len(partial) == 1:
            return partial[0]
        if not partial and not exact:
            words = [w for w in re.split(r"[\s_]+", str(name).lower()) if w]
            scored = sorted(
                ((sum(w in n.lower() for w in words), n) for n in names), key=lambda s: (-s[0], s[1]))
            similar = [n for score, n in scored if score > 0 and score == scored[0][0]]
            hint = f"; did you mean: {', '.join(similar[:8])}" if similar else ""
            raise ActionError(f"unknown component '{name}'{hint}")
        candidates = exact or partial
        raise ActionError(
            f"component name '{name}' is ambiguous: {', '.join(sorted(candidates)[:8])}")

    def resolve_property(self, component, path):
        if not path:
            raise ActionError("a property path is required")
        paths = self._backend.component_property_paths(component)
        if path in paths:
            return path
        wanted = _normalize(path)
        exact = [p for p in paths if _normalize(p) == wanted]
        if len(exact) == 1:
            return exact[0]
        leaf = [p for p in paths if _normalize(p.split("|")[-1]) == wanted]
        if len(leaf) == 1:
            return leaf[0]
        suffix = [p for p in paths if _normalize(p).endswith(wanted)]
        if len(suffix) == 1:
            return suffix[0]
        candidates = exact or leaf or suffix
        if candidates:
            raise ActionError(
                f"property '{path}' is ambiguous: {', '.join(candidates[:8])}")
        raise ActionError(
            f"no property '{path}'; available: {', '.join(paths[:20])}"
            + (" ..." if len(paths) > 20 else ""))

    # ---- execution ----

    def run(self, actions):
        """Run all actions; returns (applied_count, messages). Stops at the
        first failure so later actions never run against a half-applied
        state; everything applied so far stays inside the undo batch."""
        messages = []
        applied = 0
        self._backend.begin_undo_batch("AI Assistant actions")
        try:
            for index, action in enumerate(actions, start=1):
                text = describe(action)
                if is_destructive(action) and not self._confirm(action, text):
                    messages.append(f"#{index} skipped (user declined): {text}")
                    continue
                try:
                    result = self._apply(action)
                except ActionError as e:
                    messages.append(f"#{index} FAILED: {text} -> {e}")
                    messages.append("stopped; remaining actions were not run")
                    break
                except Exception as e:  # any Editor-side error is reported, never raised
                    messages.append(f"#{index} FAILED: {text} -> {type(e).__name__}: {e}")
                    messages.append("stopped; remaining actions were not run")
                    break
                applied += 1
                messages.append(f"#{index} ok: {text}" + (f" ({result})" if result else ""))
        finally:
            self._backend.end_undo_batch()
        for message in messages:
            self._log(message)
        return applied, messages

    def _apply(self, action):
        handlers = {
            "create_entity": self._do_create_entity,
            "delete_entity": self._do_delete_entity,
            "rename_entity": self._do_rename_entity,
            "set_parent": self._do_set_parent,
            "select": self._do_select,
            "set_transform": self._do_set_transform,
            "add_component": self._do_add_component,
            "remove_component": self._do_remove_component,
            "set_property": self._do_set_property,
            "rebuild_csharp": self._do_rebuild_csharp,
            "run_console": self._do_run_console,
            "save_level": self._do_save_level,
        }
        return handlers[action["action"]](action)

    def _do_create_entity(self, action):
        name = str(action.get("name") or "Entity")
        parent = self.resolve_parent(action.get("parent"))
        position = _vector3(action.get("position"), "position") if action.get("position") is not None else None
        entity_id = self._backend.create_entity(name, parent, position)
        self._created[_normalize(name)] = entity_id
        return self._backend.entity_id_string(entity_id)

    def _do_delete_entity(self, action):
        entity_id = self.resolve_entity(action.get("entity"))
        self._backend.delete_entity(entity_id)
        return None

    def _do_rename_entity(self, action):
        entity_id = self.resolve_entity(action.get("entity"))
        name = action.get("name")
        if not name:
            raise ActionError("rename_entity needs a non-empty \"name\"")
        self._backend.rename_entity(entity_id, str(name))
        self._created[_normalize(name)] = entity_id
        return None

    def _do_set_parent(self, action):
        entity_id = self.resolve_entity(action.get("entity"))
        parent = self.resolve_parent(action.get("parent"))
        if parent is not None and parent == entity_id:
            raise ActionError("an entity cannot be its own parent")
        self._backend.set_parent(entity_id, parent)
        return None

    def _do_select(self, action):
        refs = action.get("entities")
        if isinstance(refs, str):
            refs = [refs]
        if not isinstance(refs, list) or not refs:
            raise ActionError("select needs a non-empty \"entities\" list")
        ids = [self.resolve_entity(ref) for ref in refs]
        self._backend.select_entities(ids)
        return f"{len(ids)} selected"

    def _do_set_transform(self, action):
        entity_id = self.resolve_entity(action.get("entity"))
        if not any(k in action for k in ("position", "rotation_degrees", "scale")):
            raise ActionError("set_transform needs position, rotation_degrees and/or scale")
        if "position" in action:
            self._backend.set_world_translation(entity_id, _vector3(action["position"], "position"))
        if "rotation_degrees" in action:
            self._backend.set_local_rotation_degrees(
                entity_id, _vector3(action["rotation_degrees"], "rotation_degrees"))
        if "scale" in action:
            scale = action["scale"]
            if isinstance(scale, list):
                if len(scale) != 3:
                    raise ActionError("scale must be a number or [x, y, z]")
                if len(set(float(s) for s in scale)) != 1:
                    raise ActionError("only uniform scale is supported; use a single number")
                scale = scale[0]
            try:
                scale = float(scale)
            except (TypeError, ValueError) as e:
                raise ActionError("scale must be a number") from e
            if scale <= 0:
                raise ActionError("scale must be positive")
            self._backend.set_local_uniform_scale(entity_id, scale)
        return None

    def _do_add_component(self, action):
        entity_id = self.resolve_entity(action.get("entity"))
        type_name = self.resolve_component_type(action.get("component"))
        self._backend.add_component(entity_id, type_name)
        return type_name

    def _do_remove_component(self, action):
        entity_id = self.resolve_entity(action.get("entity"))
        type_name = self.resolve_component_type(action.get("component"))
        component = self._backend.get_component(entity_id, type_name)
        if component is None:
            raise ActionError(f"entity has no '{type_name}' component")
        self._backend.remove_component(component)
        return type_name

    def _do_set_property(self, action):
        entity_id = self.resolve_entity(action.get("entity"))
        type_name = self.resolve_component_type(action.get("component"))
        component = self._backend.get_component(entity_id, type_name)
        if component is None:
            raise ActionError(f"entity has no '{type_name}' component (add it first)")
        path = self.resolve_property(component, action.get("property"))
        if "value" not in action:
            raise ActionError("set_property needs a \"value\"")
        ok = self._backend.set_property(component, path, action["value"], self.resolve_entity)
        if not ok:
            raise ActionError(f"the Editor rejected the value for '{path}'")
        return path

    def _do_rebuild_csharp(self, action):
        self._backend.run_console("csharp_rebuild")
        return None

    def _do_run_console(self, action):
        command = str(action.get("command") or "").strip()
        if not command:
            raise ActionError("run_console needs a \"command\"")
        self._backend.run_console(command)
        return None

    def _do_save_level(self, action):
        self._backend.save_level()
        return None


def _vector3(value, what):
    if not isinstance(value, (list, tuple)) or len(value) != 3:
        raise ActionError(f"{what} must be [x, y, z]")
    try:
        return tuple(float(v) for v in value)
    except (TypeError, ValueError) as e:
        raise ActionError(f"{what} must contain numbers") from e


# ---- prompt context ----

_MAX_LISTED_ENTITIES = 150
_MAX_PROPERTIES_PER_COMPONENT = 40
_MAX_CONTEXT_CHARS = 12000


def scene_context(backend):
    """Text describing the open level for the system prompt: every entity
    (name, [id], parent) plus components and property paths of the selected
    entities. '' when no level is open."""
    try:
        entities = backend.list_entities()
    except Exception:
        return ""
    if not entities:
        return ""
    names = {backend.entity_id_string(e): n for e, n, _ in entities}
    lines = [f"Entities in the open level ({len(entities)}), as name [id] (parent):"]
    for entity_id, name, parent_id in entities[:_MAX_LISTED_ENTITIES]:
        parent = names.get(backend.entity_id_string(parent_id), "") if parent_id is not None else ""
        lines.append(f"- {name} {backend.entity_id_string(entity_id)}"
                     + (f" (parent: {parent})" if parent else ""))
    if len(entities) > _MAX_LISTED_ENTITIES:
        lines.append(f"- ... {len(entities) - _MAX_LISTED_ENTITIES} more")

    try:
        selected = backend.selected_entities()
    except Exception:
        selected = []
    if selected:
        lines.append("")
        lines.append("Selected entities (components and property paths):")
        for entity_id in selected:
            lines.append(f"* {backend.entity_name(entity_id)} {backend.entity_id_string(entity_id)}")
            try:
                components = backend.entity_components(entity_id)
            except Exception:
                components = []
            for type_name, component in components:
                try:
                    paths = backend.component_property_paths(component)
                except Exception:
                    paths = []
                shown = paths[:_MAX_PROPERTIES_PER_COMPONENT]
                values = []
                for path in shown:
                    try:
                        values.append(f"{path}={backend.get_property_text(component, path)}")
                    except Exception:
                        values.append(path)
                more = f" (+{len(paths) - len(shown)} more)" if len(paths) > len(shown) else ""
                lines.append(f"  - {type_name}: " + ("; ".join(values) if values else "(no properties)") + more)
    else:
        lines.append("")
        lines.append("No entity is selected. Select one to see its components and properties.")
    text = "\n".join(lines)
    if len(text) > _MAX_CONTEXT_CHARS:
        text = text[:_MAX_CONTEXT_CHARS] + "\n... (truncated)"
    return text


class EditorBackend:
    """azlmbr implementation. Only constructed inside the Editor."""

    def __init__(self):
        import azlmbr.bus
        import azlmbr.editor
        import azlmbr.entity
        import azlmbr.math
        import azlmbr.components
        import azlmbr.asset
        import azlmbr.legacy.general
        self._az = azlmbr
        self._type_names = None
        self._type_ids = {}
        self._id_cache = {}

    # -- entities --

    def _level_root(self):
        """The level container entity (top-level parent), or an invalid id."""
        az = self._az
        root = az.editor.ToolsApplicationRequestBus(az.bus.Broadcast, "GetCurrentLevelEntityId")
        return root if root is not None else az.entity.EntityId()

    def list_entities(self):
        az = self._az
        search_filter = az.entity.SearchFilter()
        ids = az.entity.SearchBus(az.bus.Broadcast, "SearchEntities", search_filter) or []
        root = self.entity_id_string(self._level_root())
        result = []
        self._id_cache = {}
        for entity_id in ids:
            if self.entity_id_string(entity_id) == root:
                continue
            name = self.entity_name(entity_id)
            parent = az.editor.EditorEntityInfoRequestBus(az.bus.Event, "GetParent", entity_id)
            if parent is not None and (not parent.IsValid() or self.entity_id_string(parent) == root):
                parent = None
            self._id_cache[self.entity_id_string(entity_id)] = entity_id
            result.append((entity_id, name, parent))
        result.sort(key=lambda item: item[1].lower())
        return result

    def entity_id_string(self, entity_id):
        if entity_id is None:
            return ""
        return entity_id.ToString()

    def entity_name(self, entity_id):
        az = self._az
        return az.editor.EditorEntityInfoRequestBus(az.bus.Event, "GetName", entity_id) or ""

    def entity_exists(self, entity_id):
        az = self._az
        return bool(az.editor.ToolsApplicationRequestBus(az.bus.Broadcast, "EntityExists", entity_id))

    def find_entity_by_id_string(self, text):
        text = text.strip()
        if not text.startswith("[") or not text.endswith("]"):
            return None
        if not self._id_cache:
            self.list_entities()
        return self._id_cache.get(text)

    def find_entities_by_name(self, name):
        az = self._az
        search_filter = az.entity.SearchFilter()
        search_filter.names = [name]
        ids = az.entity.SearchBus(az.bus.Broadcast, "SearchEntities", search_filter) or []
        if ids:
            return list(ids)
        wanted = _normalize(name)
        return [e for e, n, _ in self.list_entities() if _normalize(n) == wanted]

    def selected_entities(self):
        az = self._az
        return list(az.editor.ToolsApplicationRequestBus(az.bus.Broadcast, "GetSelectedEntities") or [])

    def select_entities(self, ids):
        az = self._az
        az.editor.ToolsApplicationRequestBus(az.bus.Broadcast, "SetSelectedEntities", list(ids))

    def create_entity(self, name, parent_id, position):
        az = self._az
        if parent_id is None:
            parent_id = self._level_root()
        if position is None:
            entity_id = az.editor.ToolsApplicationRequestBus(az.bus.Broadcast, "CreateNewEntity", parent_id)
        else:
            entity_id = az.editor.ToolsApplicationRequestBus(
                az.bus.Broadcast, "CreateNewEntityAtPosition", az.math.Vector3(*position), parent_id)
        if entity_id is None or not entity_id.IsValid():
            raise ActionError("the Editor did not create the entity (is a level open?)")
        az.editor.EditorEntityAPIBus(az.bus.Event, "SetName", entity_id, name)
        self._id_cache[self.entity_id_string(entity_id)] = entity_id
        return entity_id

    def delete_entity(self, entity_id):
        az = self._az
        az.editor.ToolsApplicationRequestBus(az.bus.Broadcast, "DeleteEntityAndAllDescendants", entity_id)

    def rename_entity(self, entity_id, name):
        az = self._az
        az.editor.EditorEntityAPIBus(az.bus.Event, "SetName", entity_id, name)

    def set_parent(self, entity_id, parent_id):
        az = self._az
        if parent_id is None:
            parent_id = self._level_root()
        az.editor.EditorEntityAPIBus(az.bus.Event, "SetParent", entity_id, parent_id)

    # -- transform --

    def set_world_translation(self, entity_id, xyz):
        az = self._az
        az.components.TransformBus(az.bus.Event, "SetWorldTranslation", entity_id, az.math.Vector3(*xyz))

    def set_local_rotation_degrees(self, entity_id, xyz):
        az = self._az
        rotation = az.math.Quaternion_CreateFromEulerAnglesDegrees(az.math.Vector3(*xyz))
        az.components.TransformBus(az.bus.Event, "SetLocalRotation", entity_id, rotation)

    def set_local_uniform_scale(self, entity_id, scale):
        az = self._az
        az.components.TransformBus(az.bus.Event, "SetLocalUniformScale", entity_id, float(scale))

    # -- components --

    def _game_entity_type(self):
        return self._az.entity.EntityType().Game

    def component_type_names(self):
        if self._type_names is None:
            az = self._az
            names = az.editor.EditorComponentAPIBus(
                az.bus.Broadcast, "BuildComponentTypeNameListByEntityType", self._game_entity_type()) or []
            names = sorted(set(str(n) for n in names))
            ids = az.editor.EditorComponentAPIBus(
                az.bus.Broadcast, "FindComponentTypeIdsByEntityType", names, self._game_entity_type()) or []
            self._type_ids = {
                name: type_id for name, type_id in zip(names, ids) if type_id is not None and not type_id.IsNull()}
            self._type_names = [n for n in names if n in self._type_ids]
        return self._type_names

    def _type_id(self, type_name):
        self.component_type_names()
        if type_name not in self._type_ids:
            raise ActionError(f"component type '{type_name}' is not available")
        return self._type_ids[type_name]

    def entity_components(self, entity_id):
        """[(type_name, component_id)] for every component on the entity."""
        az = self._az
        result = []
        for type_name in self.component_type_names():
            type_id = self._type_ids[type_name]
            outcome = az.editor.EditorComponentAPIBus(az.bus.Broadcast, "GetComponentsOfType", entity_id, type_id)
            if outcome is not None and outcome.IsSuccess():
                for component in outcome.GetValue():
                    result.append((type_name, component))
        return result

    def get_component(self, entity_id, type_name):
        az = self._az
        outcome = az.editor.EditorComponentAPIBus(
            az.bus.Broadcast, "GetComponentOfType", entity_id, self._type_id(type_name))
        if outcome is None or not outcome.IsSuccess():
            return None
        return outcome.GetValue()

    def add_component(self, entity_id, type_name):
        az = self._az
        outcome = az.editor.EditorComponentAPIBus(
            az.bus.Broadcast, "AddComponentsOfType", entity_id, [self._type_id(type_name)])
        if outcome is None or not outcome.IsSuccess():
            raise ActionError(f"the Editor could not add '{type_name}' (incompatible or missing service?)")
        return outcome.GetValue()[0]

    def remove_component(self, component):
        az = self._az
        if not az.editor.EditorComponentAPIBus(az.bus.Broadcast, "RemoveComponents", [component]):
            raise ActionError("the Editor could not remove the component")

    def component_property_paths(self, component):
        az = self._az
        paths = az.editor.EditorComponentAPIBus(az.bus.Broadcast, "BuildComponentPropertyList", component) or []
        # Group nodes ("A|B") and nameless leaves ("A|B|") are not settable.
        return sorted(str(p) for p in paths if str(p) and not str(p).endswith("|")
                      and self._current_value(component, str(p)) is not None)

    @staticmethod
    def _type_name(value):
        """Behavior-class name of a value (math types arrive as PythonProxyObject)."""
        if value is None:
            return ""
        if type(value).__name__ == "PythonProxyObject":
            return str(value.typename)
        return type(value).__name__

    def get_property_text(self, component, path):
        value = self._current_value(component, path)
        if value is None:
            return "?"
        if type(value).__name__ == "PythonProxyObject":
            if self._type_name(value) == "AssetId":
                if not value.IsValid():
                    return "(no asset)"
                path = self._az.asset.AssetCatalogRequestBus(self._az.bus.Broadcast, "GetAssetPathById", value)
                return str(path) if path else str(value.ToString())
            to_string = value.ToString
            if callable(to_string):
                return str(to_string())
            return f"<{self._type_name(value)}>"
        return str(value)

    def _current_value(self, component, path):
        az = self._az
        outcome = az.editor.EditorComponentAPIBus(az.bus.Broadcast, "GetComponentProperty", component, path)
        if outcome is None or not outcome.IsSuccess():
            return None
        try:
            value = outcome.GetValue()
        except (SystemError, TypeError, RuntimeError):
            return None  # type not exposed to Python
        if self._type_name(value) == "AZStd::any":
            return None  # group node, not a settable leaf
        return value

    def coerce_value(self, value, current, resolve_entity):
        """Convert a JSON value into the azlmbr type of the current value."""
        try:
            return self._coerce_value(value, current, resolve_entity)
        except (TypeError, ValueError) as e:
            current_type = self._type_name(current) or "unknown"
            raise ActionError(f"value {value!r} does not fit the property type ({current_type}): {e}") from e

    def _coerce_value(self, value, current, resolve_entity):
        az = self._az
        current_type = self._type_name(current)
        if isinstance(value, list):
            if isinstance(current, list):
                return value
            numbers = [float(v) for v in value]
            if current_type == "Quaternion" or (len(numbers) == 4 and current_type not in ("Color", "Vector4")):
                return az.math.Quaternion(*numbers)
            if current_type == "Color":
                if len(numbers) == 3:
                    numbers.append(1.0)
                return az.math.Color(*numbers)
            if current_type == "Vector4":
                return az.math.Vector4(*numbers)
            if current_type == "Vector2":
                return az.math.Vector2(*numbers)
            return az.math.Vector3(*numbers)
        if isinstance(value, str):
            if current_type == "AssetId" or value.lower().endswith(
                    (".azmodel", ".azmaterial", ".streamingimage", ".spawnable", ".luac", ".pxmesh",
                     ".azshader", ".azasset", ".attimage", ".animgraph", ".motionset", ".actor", ".fbx")):
                asset_id = az.asset.AssetCatalogRequestBus(
                    az.bus.Broadcast, "GetAssetIdByPath", value, az.math.Uuid(), False)
                if asset_id is None or not asset_id.IsValid():
                    raise ActionError(
                        f"asset '{value}' is not in the asset catalog (check the path/extension, "
                        "or wait for the Asset Processor)")
                return asset_id
            if current_type == "EntityId":
                return resolve_entity(value)
            if isinstance(current, bool):
                lowered = value.strip().lower()
                if lowered in ("true", "1", "yes", "on"):
                    return True
                if lowered in ("false", "0", "no", "off"):
                    return False
                raise ActionError(f"'{value}' is not a boolean")
            if isinstance(current, int):
                return int(float(value))
            if isinstance(current, float):
                return float(value)
            return value
        if isinstance(value, bool):
            return value
        if isinstance(value, (int, float)):
            if isinstance(current, bool):
                return bool(value)
            if isinstance(current, int):
                return int(value)
            return float(value)
        raise ActionError(f"unsupported value {value!r}")

    def set_property(self, component, path, value, resolve_entity):
        az = self._az
        current = self._current_value(component, path)
        value = self.coerce_value(value, current, resolve_entity)
        outcome = az.editor.EditorComponentAPIBus(az.bus.Broadcast, "SetComponentProperty", component, path, value)
        return bool(outcome is not None and outcome.IsSuccess())

    # -- misc --

    def run_console(self, command):
        self._az.legacy.general.run_console(command)

    def save_level(self):
        self._az.legacy.general.save_level()

    def begin_undo_batch(self, label):
        az = self._az
        az.editor.ToolsApplicationRequestBus(az.bus.Broadcast, "BeginUndoBatch", label)

    def end_undo_batch(self):
        az = self._az
        az.editor.ToolsApplicationRequestBus(az.bus.Broadcast, "EndUndoBatch")
