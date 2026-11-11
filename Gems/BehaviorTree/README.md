# BehaviorTree

BehaviorTree is an O3DE runtime gem for data-driven AI behavior trees. Enable it
in a project with the VoxelNav gem enabled, then add a **Behavior Tree**
component (category **AI**) to an entity and assign a project-relative
`.behaviortree.json` file. A VoxelNav volume is required by `MoveTo`.

The **Behavior Tree Editor** is available from **Tools** and from the component's
**Open in Behavior Tree Editor** button. It edits the same schema used by the
runtime parser, validates documents, shows the blackboard, and polls active
runtime nodes and blackboard values during game mode.

## Node reference

| Category | Node | Parameters | Behavior |
| --- | --- | --- | --- |
| Composite | `Sequence` | `reactive=false` | Runs children in order; remembers a running child. |
| Composite | `Selector` | `reactive=false` | Runs children until one succeeds; remembers a running child. |
| Composite | `Parallel` | `successThreshold=-1`, `failureThreshold=1` | Ticks unfinished children until a threshold is met; aborts remaining runners. |
| Decorator | `Inverter` | — | Swaps success and failure. |
| Decorator | `AlwaysSucceed` | — | Succeeds when its child finishes. |
| Decorator | `Repeat` | `count=-1`, `untilFailure=false` | Repeats a child; `count=-1` is unbounded. |
| Decorator | `Cooldown` | `seconds=0` | Fails without ticking the child during cooldown. |
| Decorator | `Timeout` | `seconds=0` | Aborts a child that exceeds its time limit. |
| Leaf | `Wait` | `seconds=1`, `randomDeviation=0` | Deterministic per-tree wait. |
| Leaf | `CheckBlackboard` | `key`, `op`, `value` | Uses typed comparisons: Exists, Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual, IsTrue, IsFalse. |
| Leaf | `SetBlackboard` | `key`, `valueType`, `value` | Sets bool, float, string, vector3, or entity values. `valueType` avoids colliding with the node's reserved JSON `type` field. |
| Leaf | `Action` | `name` | Calls the entity's action notification handler. |
| Leaf | `Condition` | `name` | Calls the entity's condition notification handler. |
| Leaf | `Log` | `message` | Sends a message to the runtime log. |
| Leaf | `MoveTo` | `targetKey`, `acceptanceRadius=0.5`, `speed=4`, `repathInterval=0.5` | Follows a VoxelNav path to a Vector3 or EntityId blackboard target. |

Composites require at least one child, decorators require exactly one, and
leaves have none. Missing parameters use schema defaults. Unknown nodes,
parameters, malformed values, and invalid child counts report node paths.

## Script hooks

Implement `BehaviorTreeActionNotificationBus` on the same entity as the
component. `OnActionTick(action, deltaTime)` returns `BehaviorTreeStatus.Success`,
`BehaviorTreeStatus.Failure`, or `BehaviorTreeStatus.Running`; `OnActionAbort(action)` is
called when a running action is interrupted; `OnCondition(condition)` returns
a boolean. Missing action handlers fail and warn once per action name.
`BehaviorTreeRequestBus` provides typed blackboard access, pause, restart, and
active node paths. C# hooks are not included.

## Limitations

- The Editor UI is a tree outline, not a node-graph canvas.
- `MoveTo` requires a ready VoxelNav volume.
- Without a Physics character controller, movement steps the entity through
  `TransformBus`; this fallback does not perform collision resolution.
- Live runtime debug requires Editor game mode and a selected entity.

Example assets: `Examples/Guard.behaviortree.json` and `Examples/GuardSenses.lua`.
