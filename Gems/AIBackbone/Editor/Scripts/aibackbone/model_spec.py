"""
Copyright (c) Contributors to the Open 3D Engine Project.
For complete copyright and license terms please see the LICENSE at the root of this distribution.

SPDX-License-Identifier: Apache-2.0 OR MIT
"""
# Model specification: a JSON-serializable description of a simple feed-forward
# network - its input variables (with types), hidden layers (units + activation),
# output fields and training hyperparameters. Specs are the "pre-built template
# structure": they can be saved, duplicated and edited, and are turned into real
# PyTorch models by torch_backend.build_model().

import json
import os

# Supported input/output variable types and how many floats each one occupies in
# the network's input/output vector.
VARIABLE_TYPES = {
    "float": 1,
    "int": 1,
    "bool": 1,
    "vec2": 2,
    "vec3": 3,
    "vec4": 4,
}

ACTIVATIONS = ["relu", "tanh", "sigmoid", "leaky_relu", "none"]

# Activation applied after the output layer.
OUTPUT_ACTIVATIONS = ["none", "sigmoid", "tanh", "softmax"]


class SpecError(ValueError):
    pass


def make_variable(name, var_type):
    if var_type not in VARIABLE_TYPES:
        raise SpecError(f"Unknown variable type '{var_type}' (valid: {', '.join(VARIABLE_TYPES)})")
    return {"name": name, "type": var_type}


def make_layer(units, activation):
    if activation not in ACTIVATIONS:
        raise SpecError(f"Unknown activation '{activation}' (valid: {', '.join(ACTIVATIONS)})")
    return {"units": int(units), "activation": activation}


def make_spec(name, inputs, layers, outputs, output_activation="none",
              epochs=100, learning_rate=1e-3, batch_size=32):
    return {
        "format": "aibackbone-model-spec",
        "version": 1,
        "name": name,
        "inputs": inputs,        # list of make_variable()
        "layers": layers,        # list of make_layer()
        "outputs": outputs,      # list of make_variable()
        "output_activation": output_activation,
        "training": {
            "epochs": int(epochs),
            "learning_rate": float(learning_rate),
            "batch_size": int(batch_size),
        },
    }


def validate_spec(spec):
    if spec.get("format") != "aibackbone-model-spec":
        raise SpecError("Not an AI Backbone model spec")
    if not spec.get("name"):
        raise SpecError("Model needs a name")
    if not spec.get("inputs"):
        raise SpecError("Model needs at least one input variable")
    if not spec.get("outputs"):
        raise SpecError("Model needs at least one output field")
    for var in list(spec["inputs"]) + list(spec["outputs"]):
        if var.get("type") not in VARIABLE_TYPES:
            raise SpecError(f"Variable '{var.get('name')}' has unknown type '{var.get('type')}'")
        if not var.get("name"):
            raise SpecError("Every variable needs a name")
    for layer in spec.get("layers", []):
        if int(layer.get("units", 0)) <= 0:
            raise SpecError("Every layer needs a positive unit count")
        if layer.get("activation") not in ACTIVATIONS:
            raise SpecError(f"Layer has unknown activation '{layer.get('activation')}'")
    if spec.get("output_activation", "none") not in OUTPUT_ACTIVATIONS:
        raise SpecError(f"Unknown output activation '{spec.get('output_activation')}'")
    return spec


def vector_width(variables):
    """Total float width of a list of variables."""
    return sum(VARIABLE_TYPES[v["type"]] for v in variables)


def save_spec(spec, path):
    validate_spec(spec)
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(spec, f, indent=4)


def load_spec(path):
    with open(path, "r", encoding="utf-8") as f:
        return validate_spec(json.load(f))


# Ready-made starting-point templates offered by the Model Builder UI.
def builtin_templates():
    return {
        "Blank": make_spec(
            "NewModel",
            inputs=[make_variable("input", "float")],
            layers=[make_layer(16, "relu")],
            outputs=[make_variable("output", "float")],
        ),
        "NPC decision (state -> action scores)": make_spec(
            "NpcDecision",
            inputs=[
                make_variable("self_position", "vec3"),
                make_variable("target_position", "vec3"),
                make_variable("health", "float"),
                make_variable("target_visible", "bool"),
            ],
            layers=[make_layer(32, "relu"), make_layer(32, "relu")],
            outputs=[
                make_variable("attack_score", "float"),
                make_variable("flee_score", "float"),
                make_variable("idle_score", "float"),
            ],
            output_activation="softmax",
            epochs=200,
        ),
        "Movement controller (sensors -> steering)": make_spec(
            "MovementController",
            inputs=[
                make_variable("velocity", "vec3"),
                make_variable("goal_direction", "vec3"),
                make_variable("obstacle_distance", "float"),
            ],
            layers=[make_layer(24, "tanh"), make_layer(24, "tanh")],
            outputs=[make_variable("steer", "vec2"), make_variable("throttle", "float")],
            output_activation="tanh",
            epochs=150,
        ),
        "Value predictor (regression)": make_spec(
            "ValuePredictor",
            inputs=[make_variable("feature_a", "float"), make_variable("feature_b", "float")],
            layers=[make_layer(16, "relu")],
            outputs=[make_variable("value", "float")],
            epochs=100,
        ),
    }
