/**
 * Neural Staged Demo — JavaScript
 *
 * Demonstrates the staged neural verbs:
 *
 *   sys.neural.loadModel(...)
 *   sys.neural.setInput(modelId, name, data, shape?)   <-- explicit
 *   sys.neural.run(modelId)                            <-- no inputs map
 *   sys.neural.getOutput(modelId, name)                <-- explicit
 *
 * Model: sigmoid.onnx (single Sigmoid op over float32[3,4,5]).
 *        Input  "x"  Float32 [3,4,5]
 *        Output "y"  Float32 [3,4,5]
 *
 * Re-uses a single Float32Array allocation across frames; the staged
 * setInput call hands the same buffer to ORT each tick after refilling
 * it with a time-varying ramp.
 *
 * Renders the 60-element output as a 12x5 heatmap (3 batches stacked
 * vertically, 4 columns each). Cells go from dark red (sigmoid≈0) to
 * bright cyan (sigmoid≈1).
 *
 * Build with `"neural": true` in app.json so the runtime initialises
 * ONNX Runtime; otherwise `sys.neural` is absent.
 */

const COLORS = {
    bg: '#101820',
    text: '#e0e0e0',
    dim: '#6c7a89',
    accent: '#4ecdc4',
    warn: '#ffb84d',
    err: '#ff6b6b',
};

const SHAPE = [3, 4, 5];
const ELEM_COUNT = SHAPE[0] * SHAPE[1] * SHAPE[2];   // 60

let modelId = -1;
let inputName = 'x';
let outputName = 'y';
let inputBuf = null;             // Float32Array, allocated ONCE
let lastOutput = null;           // Float32Array reference (read-only view)
let statusLine = 'initialising';
let neuralAvailable = false;
let frameCount = 0;

// ── Capability probe + setup ───────────────────────────────────────────────

const capProbe = sys.capabilities && sys.capabilities.neural;
neuralAvailable = !!(capProbe && capProbe.available);

if (!neuralAvailable) {
    statusLine = 'sys.capabilities.neural.available is false (build without ENABLE_NEURAL?)';
} else if (!sys.neural || !sys.neural.isAvailable()) {
    neuralAvailable = false;
    statusLine = 'sys.neural absent (set "neural": true in app.json)';
} else {
    try {
        modelId = sys.neural.loadModel('sigmoid.onnx');
        const info = sys.neural.getModelInfo(modelId);
        inputName = info.inputs[0].name;
        outputName = info.outputs[0].name;
        inputBuf = new Float32Array(ELEM_COUNT);
        statusLine = 'staged loop: setInput → run → getOutput';
        sys.log('[neural_staged] loaded sigmoid.onnx as model id', modelId,
            '| input =', inputName, JSON.stringify(info.inputs[0].shape),
            '| output =', outputName, JSON.stringify(info.outputs[0].shape));
    } catch (e) {
        neuralAvailable = false;
        statusLine = 'loadModel failed: ' + (e && e.message ? e.message : e);
    }
}

// ── Refill the reused input buffer with a time-varying ramp ────────────────
// Range roughly [-6, +6] so sigmoid outputs sweep the full (0, 1) range.

function refillInput(t) {
    const phase = (t * 0.0015);
    for (let i = 0; i < ELEM_COUNT; i++) {
        // -6 .. +6 ramp, offset per-element so different cells move at
        // different phases — the heatmap will visibly ripple.
        const u = (i / ELEM_COUNT) * Math.PI * 2;
        inputBuf[i] = 6.0 * Math.sin(u + phase);
    }
}

// ── Frame loop ─────────────────────────────────────────────────────────────

function drawHeatmap(out, x0, y0, cellW, cellH) {
    // Lay out the [3,4,5] tensor as 3 row-bands of 4×5 cells.
    for (let b = 0; b < SHAPE[0]; b++) {
        for (let r = 0; r < SHAPE[1]; r++) {
            for (let c = 0; c < SHAPE[2]; c++) {
                const v = out[b * SHAPE[1] * SHAPE[2] + r * SHAPE[2] + c];
                // v ∈ (0, 1). Map to RGB: low → deep red, high → cyan.
                const red = Math.round(255 * (1 - v));
                const green = Math.round(255 * v * 0.85);
                const blue = Math.round(255 * v);
                const hex = '#' +
                    red.toString(16).padStart(2, '0') +
                    green.toString(16).padStart(2, '0') +
                    blue.toString(16).padStart(2, '0');
                sys.canvas.setFillColor(hex);
                const x = x0 + (b * SHAPE[2] + c) * cellW;
                const y = y0 + r * cellH;
                sys.canvas.drawRect(x + 1, y + 1, cellW - 2, cellH - 2);
            }
        }
    }
}

function frame() {
    const t = sys.input.get().totalTime;
    const W = sys.window.getWidth();
    const H = sys.window.getHeight();

    sys.canvas.clear(COLORS.bg);

    // Title + status
    sys.canvas.setFillColor(COLORS.text);
    sys.canvas.drawText('Neural Staged Demo — sigmoid.onnx', 20, 36, 22);
    sys.canvas.setFillColor(COLORS.dim);
    sys.canvas.drawText(statusLine, 20, 60, 13);

    if (neuralAvailable && modelId >= 0) {
        // ── Staged inference: setInput → run → getOutput ──────────────────
        refillInput(t);
        sys.neural.setInput(modelId, inputName, inputBuf, SHAPE);
        sys.neural.run(modelId);                              // <-- no inputs arg
        const out = sys.neural.getOutput(modelId, outputName);
        lastOutput = out;

        // Heatmap geometry
        const cellW = 36;
        const cellH = 36;
        const gridW = SHAPE[0] * SHAPE[2] * cellW;
        const gridH = SHAPE[1] * cellH;
        const x0 = Math.floor((W - gridW) / 2);
        const y0 = Math.floor((H - gridH) / 2) + 10;

        sys.canvas.setFillColor(COLORS.accent);
        sys.canvas.drawText('sigmoid([3,4,5]) — animated', x0, y0 - 10, 12);

        drawHeatmap(out, x0, y0, cellW, cellH);

        // Periodic sanity log so a SIGINT'd 3-second run shows progress.
        frameCount++;
        if (frameCount % 60 === 0) {
            let mn = out[0], mx = out[0], sum = 0;
            for (let i = 0; i < out.length; i++) {
                const v = out[i];
                if (v < mn) mn = v;
                if (v > mx) mx = v;
                sum += v;
            }
            const mean = sum / out.length;
            sys.log('[neural_staged] frame', frameCount,
                '| out min=', mn.toFixed(4),
                'max=', mx.toFixed(4),
                'mean=', mean.toFixed(4));
        }
    } else {
        sys.canvas.setFillColor(COLORS.err);
        sys.canvas.drawText(statusLine, 20, H / 2, 14);
    }

    sys.animation.requestFrame(frame);
}

sys.animation.requestFrame(frame);
