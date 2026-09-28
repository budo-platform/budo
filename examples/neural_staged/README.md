# Neural Staged Demo

Smallest possible showcase of the **staged neural verbs**.

```
sys.neural.loadModel('sigmoid.onnx')
sys.neural.setInput(modelId, 'x', float32Buf, [3, 4, 5])
sys.neural.run(modelId)                   // no inputs map
sys.neural.getOutput(modelId, 'y')
```

The same `Float32Array` is reused across every frame — only its contents are
refilled in `refillInput()`. This is the recommended pattern for tight
inference loops on the desktop and Android targets.

The model is `sigmoid.onnx` — a 1-op ONNX graph that applies element-wise
sigmoid to a `float32[3, 4, 5]` tensor. The 60 outputs are rendered as a
12×5 heatmap (three 4×5 batches side by side); the input is animated so the
heatmap ripples.

`sys.neural` is desktop + Android only (it requires ONNX Runtime). On web,
`sys.capabilities.neural.available` returns `false` and the example renders a
red error line.

## Regenerating sigmoid.onnx

```bash
examples/collet-ai-train/.venv/bin/python3 \
    examples/neural_staged/generate_model.py
```

A periodic `[neural_staged] frame N | out min=… max=… mean=…` line is
printed every 60 frames so a short smoke run shows live progress.
