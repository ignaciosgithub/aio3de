# AI Backbone Gem

In-engine neural network backbone: create, train, stop/resume, import and run ML
models from an Editor tool, with a Python API for recording gameplay data into
training datasets. Backed by PyTorch and ONNX running in the engine's own Python
environment.

## Setup

1. Install the ML libraries into the engine's Python environment (one time):

   ```bat
   REM Windows (from the engine root)
   Gems\AIBackbone\install_ai_libs.bat          REM CPU PyTorch
   Gems\AIBackbone\install_ai_libs.bat --cuda   REM CUDA PyTorch (NVIDIA, incl. RTX 50-series)
   ```

   ```bash
   # Linux/macOS
   Gems/AIBackbone/install_ai_libs.sh [--cuda]
   ```

2. Enable the gem for your project (it depends on `EditorPythonBindings` and
   `QtForPython`, which are enabled by default):

   ```bat
   scripts\o3de.bat enable-gem -gn AIBackbone -pp C:\path\to\YourProject
   ```

3. Launch the Editor and open **Tools → AI Model Builder**.

## AI Model Builder (Editor tool)

- **Models tab** — create models from pre-built templates (NPC decision,
  movement controller, value predictor, blank) and edit everything about them:
  input variables (name + type: `float`, `int`, `bool`, `vec2`, `vec3`, `vec4`),
  hidden layers (units + activation), output fields, output activation, epochs,
  learning rate and batch size. Specs are saved as JSON under
  `<project>/AIModels/<name>.model.json` — duplicate/edit them freely.
- **Train tab** — pick a model and a recorded dataset and train in the
  background with a live loss readout. **Stop & Save** checkpoints the model at
  any moment (a checkpoint is also written automatically every epoch), and
  **Start / Resume** continues from the checkpoint exactly where it stopped —
  no work is lost if training is interrupted or turns out heavier than the
  machine can handle. Finished/stopped models are saved as `<name>.pt` plus an
  ONNX export `<name>.onnx`.
- **Import ONNX tab** — import any existing ONNX model (e.g. open-source ones)
  and auto-detect its required inputs and outputs (names, dtypes, shapes).

## Recording training data in-engine

From any engine Python script (Editor scripts, `azlmbr`-driven tooling):

```python
from aibackbone.recorder import DatasetRecorder

rec = DatasetRecorder("AIModels/npc_decisions.jsonl")
rec.record(
    inputs={"self_position": [x, y, z], "target_position": [tx, ty, tz],
            "health": hp, "target_visible": visible},
    outputs={"attack_score": 1.0, "flee_score": 0.0, "idle_score": 0.0})
rec.close()
```

Field names and types must match the model spec's input/output variables. Every
sample is flushed to disk immediately, so recorded data survives crashes.

## Running models in-engine

```python
from aibackbone import model_spec, torch_backend

spec = model_spec.load_spec("AIModels/NpcDecision.model.json")
model = torch_backend.load_trained_model(spec, "AIModels/NpcDecision.model.json")
scores = torch_backend.predict(spec, model, {
    "self_position": [0, 0, 0], "target_position": [1, 2, 0],
    "health": 0.8, "target_visible": 1.0})
```

Imported ONNX models run through `aibackbone.onnx_inspect.OnnxModel`.
