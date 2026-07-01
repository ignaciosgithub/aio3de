"""
Copyright (c) Contributors to the Open 3D Engine Project.
For complete copyright and license terms please see the LICENSE at the root of this distribution.

SPDX-License-Identifier: Apache-2.0 OR MIT
"""
# ONNX model import + inspection: detects the required inputs and outputs
# (names, shapes, dtypes) of any ONNX model, including imported open-source ones,
# and can run inference on them through onnxruntime.


def inspect_model(path):
    """Returns {"inputs": [...], "outputs": [...]} where each entry is
    {"name", "dtype", "shape"} (shape entries are ints or symbolic dim names)."""
    import onnx

    model = onnx.load(path)

    def describe(values, initializer_names):
        described = []
        for value in values:
            if value.name in initializer_names:
                continue  # weights, not a real runtime input
            tensor = value.type.tensor_type
            shape = []
            for dim in tensor.shape.dim:
                shape.append(dim.dim_value if dim.dim_value > 0 else (dim.dim_param or "dynamic"))
            described.append({
                "name": value.name,
                "dtype": onnx.TensorProto.DataType.Name(tensor.elem_type).lower(),
                "shape": shape,
            })
        return described

    initializer_names = {init.name for init in model.graph.initializer}
    return {
        "inputs": describe(model.graph.input, initializer_names),
        "outputs": describe(model.graph.output, set()),
    }


class OnnxModel:
    """Loaded ONNX model runnable in-engine through onnxruntime.

    Example:
        model = OnnxModel("AIModels/imported.onnx")
        print(model.io)                      # detected inputs/outputs
        outputs = model.run({"inputs": [[0.1, 0.2, 0.3]]})
    """

    def __init__(self, path):
        import onnxruntime

        self.path = path
        self.session = onnxruntime.InferenceSession(path, providers=["CPUExecutionProvider"])
        self.io = inspect_model(path)

    def run(self, feeds):
        import numpy

        arrays = {name: numpy.asarray(value, dtype=numpy.float32) for name, value in feeds.items()}
        output_names = [out["name"] for out in self.io["outputs"]]
        results = self.session.run(output_names, arrays)
        return dict(zip(output_names, (r.tolist() for r in results)))
