#!/usr/bin/env python3
"""
Generate sigmoid.onnx — a 1-op ONNX model that applies Sigmoid element-wise to
a float32[3,4,5] tensor named "x" and returns "y" with the same shape.

Used by examples/neural_staged to demonstrate the staged neural verbs
(loadModel -> setInput -> run -> getOutput).

Run from any environment that has the `onnx` package available:

    examples/collet-ai-train/.venv/bin/python3 \
        examples/neural_staged/generate_model.py

Requires: onnx (already in the collet-ai-train venv).
"""
import onnx
from onnx import TensorProto, helper


def main():
    x = helper.make_tensor_value_info("x", TensorProto.FLOAT, [3, 4, 5])
    y = helper.make_tensor_value_info("y", TensorProto.FLOAT, [3, 4, 5])
    node = helper.make_node("Sigmoid", inputs=["x"], outputs=["y"])
    graph = helper.make_graph([node], "sigmoid", [x], [y])
    model = helper.make_model(
        graph,
        opset_imports=[helper.make_opsetid("", 13)],
        producer_name="budo/examples/neural_staged",
    )
    model.ir_version = 7
    onnx.checker.check_model(model)
    onnx.save(model, "sigmoid.onnx")
    print("wrote sigmoid.onnx")


if __name__ == "__main__":
    main()
#!/usr/bin/env python3
"""
Generate sigmoid.onnx — a 1-op ONNX model that applies Sigmoid element-wise to
a float32[3,4,5] tensor named "x" and returns "y" with the same shape.

Used by examples/neural_staged to demonstrate the staged neural verbs
(loadModel -> setInput -> run -> getOutput).

Run from the collet-ai-train .venv:

    examples/collet-ai-train/.venv/bin/python3 \
        examples/neural_staged/generate_model.py

Requires: onnx (already in the collet-ai-train venv).
"""
import onnx
from onnx import TensorProto, helper


def main():
    x = helper.make_tensor_value_info("x", TensorProto.FLOAT, [3, 4, 5])
    y = helper.make_tensor_value_info("y", TensorProto.FLOAT, [3, 4, 5])
    node = helper.make_node("Sigmoid", inputs=["x"], outputs=["y"])
    graph = helper.make_graph([node], "sigmoid", [x], [y])
    model = helper.make_model(
        graph,
        opset_imports=[helper.make_opsetid("", 13)],
        producer_name="budo/examples/neural_staged",
    )
    model.ir_version = 7
    onnx.checker.check_model(model)
    onnx.save(model, "sigmoid.onnx")
    print("wrote sigmoid.onnx")


if __name__ == "__main__":
    main()
#!/usr/bin/env python3
"""
Generate sigmoid.onnx — a 1-op model that applies Sigmoid element-wise to a
float32[3,4,5] tensor named "x" and returns "y" with the same shape.

Used by examples/neural_staged to demonstrate the staged neural verbs
(loadModel → setInput → run → getOutput).

Run from the collet-ai-train .venv:

    cd examples/collet-ai-train
    source .venv/bin/activate
    cd ../neural_staged
    python3 generate_model.py

Requires: torch (already in the collet-ai-train venv).
"""
import torch
import torch.nn as nn


class SigmoidModel(nn.Module):
    def forward(self, x):
        return torch.sigmoid(x)


def main():
    model = SigmoidModel().eval()
    dummy = torch.zeros(3, 4, 5, dtype=torch.float32)
    torch.onnx.export(
        model,
        dummy,
        "sigmoid.onnx",
        input_names=["x"],
        output_names=["y"],
        opset_version=13,
        dynamic_axes=None,  # fully static [3,4,5]
    )
    print("wrote sigmoid.onnx")


if __name__ == "__main__":
    main()
