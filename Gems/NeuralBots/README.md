# Neural Bots Gem

Neural-network-driven combatants for AI PvP, with **human constraints** so bots
compete like players, not aimbots. A **Bot Agent** component perceives a target,
decides through an MLP policy (trained with the **AIBackbone** gem) or a
built-in chase/strafe/shoot heuristic, and acts through the same channels a
human uses: character-controller movement, capped-speed view rotation, and
hitscan shots that send `Damage` gameplay events (compatible with the
ArenaShooter kit's `Health.lua`).

## Enable

```
scripts\o3de.bat enable-gem -gn NeuralBots -pp <your project path>
```

Code gem: re-run the CMake configure and rebuild the Editor afterwards.
Requires PhysX (bots move via the PhysX character controller).

## Bot entity setup

1. Entity with PhysX **Character Controller** (+ optional Character Gameplay).
2. Add **Bot Agent** (Add Component → AI → Bot Agent).
3. Set **Target entity** to the player (or another bot for AI-vs-AI).
4. Optionally add the ArenaShooter `Health.lua` so the bot can die/respawn.
5. Leave **Model file** empty to use the built-in heuristic — bots fight out
   of the box.

## Human constraints

| Setting | Meaning | Default |
| --- | --- | --- |
| Reaction time | delay between the world changing and the bot perceiving it | 0.20 s |
| Aim error | std-dev of Gaussian noise applied to every shot | 2.5° |
| Max turn speed | view rotation cap | 360°/s |
| Max shots per second | fire-rate cap | 5 |

The bot also only shoots with line of sight (checked by physics raycast), and
its perception is a delayed snapshot — it aims at where the target *was* a
reaction-time ago.

## Training a policy with AIBackbone

The policy contract (fixed observation/action layout):

- **Inputs (8 floats)**: target direction in bot-local space (x, y, z),
  distance/50, line-of-sight (0/1), own speed/10, cos(angle to target),
  time-since-seen/2.
- **Outputs (5 floats)**: strafe (-1..1), forward (-1..1), turn (-1..1 of max
  turn speed), shoot (>0.5 fires), jump (>0.5, reserved).

Workflow:

1. In the AI Model Builder (AIBackbone gem), create a model with 8 float
   inputs and 5 float outputs (e.g. two hidden layers of 32, `tanh` output).
2. Record a dataset (e.g. from your own play using the recorder API) and train.
3. Training now also exports `<model>.weights.json` next to the `.pt`/`.onnx`.
4. Put that file in your project and set the Bot Agent's **Model file** to its
   project-relative path (e.g. `AIModels/pvp_bot.weights.json`).
5. If the file is missing or the widths don't match, the bot logs a warning
   and falls back to the heuristic.
