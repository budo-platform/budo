# neural_demo

Demonstrates the `sys.neural` API using a pre-quantized **SqueezeNet 1.1**
model from the [ONNX Model Zoo](https://github.com/onnx/models).

The demo:
1. Loads `squeezenet1.1-7.onnx` from the example directory.
2. Displays model metadata (producer, graph name, input/output tensor schema).
3. Runs inference every 30 frames on a synthetic gradient image, showing
   top-1 class index, confidence, and inference latency.

Both a **JavaScript** (`main.js`) and a **Lua** (`main.lua`) entry point are
provided — the runtime auto-selects `main.js` by default; rename or remove it
to try the Lua version.

## Prerequisites

- Budo must be built with `ENABLE_NEURAL=ON` (the default).
  Run `./scripts/setup-onnx.sh` first if `third_party/onnxruntime/` is absent.
- `"neural": true` is already set in `app.json`; no additional configuration
  is needed.

## Download the model

The model file is **not** committed to the repository due to its size (~1.2 MB).
Download it before running the demo:

```bash
# from the repository root
curl -L \
  "https://github.com/onnx/models/raw/main/validated/vision/classification/squeezenet/model/squeezenet1.1-7.onnx" \
  -o examples/neural_demo/squeezenet1.1-7.onnx
```

Or using `wget`:

```bash
wget -O examples/neural_demo/squeezenet1.1-7.onnx \
  "https://github.com/onnx/models/raw/main/validated/vision/classification/squeezenet/model/squeezenet1.1-7.onnx"
```

## Model details

| Property   | Value                    |
|------------|--------------------------|
| File       | `squeezenet1.1-7.onnx`   |
| Size       | ~1.2 MB                  |
| Format     | ONNX opset 7             |
| Input      | `data` — Float32 `[1, 3, 224, 224]` (NCHW, values in `[0, 1]`) |
| Output     | `softmaxout_1` — Float32 `[1, 1000]` (ImageNet class probabilities) |
| Exec. provider | CPU always; CoreML on macOS; NNAPI on Android (silent CPU fallback) |

## Expected output

The demo renders:
- **Top-1 class index** and **confidence** for the synthetic gradient input.
- **Inference latency** in milliseconds.
- The full **tensor schema** (names, shapes, dtypes) read from the model.

Because the input is a synthetic gradient and not a real image, the predicted
class is meaningless — the goal is to verify that the inference pipeline works
end-to-end and to measure latency on the target hardware.
