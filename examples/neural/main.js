/**
 * Neural Demo — JavaScript
 *
 * Demonstrates the sys.neural API using a pre-quantized SqueezeNet 1.1 model
 * (squeezenet1.1-7.onnx, ~1.2 MB) from the ONNX Model Zoo.
 *
 * Expected model: squeezenet1.1-7.onnx in the same directory as this file.
 * Input:  "data"          Float32 [1, 3, 224, 224]
 * Output: "softmaxout_1"  Float32 [1, 1000]
 *
 * See README.md for download instructions.
 */

// ============================================
// State
// ============================================

const COLORS = {
    bg: '#1a1a2e',
    panel: '#16213e',
    border: '#0f3460',
    accent: '#e94560',
    text: '#e0e0e0',
    dim: '#888888',
    success: '#4ecdc4',
    warn: '#ffeaa7',
};

let modelId = -1;
let modelInfo = null;
let statusLine = 'Initializing...';
let inferenceMs = 0;
let topIdx = -1;
let topConf = 0;
let frameCount = 0;
let lastFpsTime = 0;
let fps = 0;
let inputPixels = 1 * 3 * 224 * 224;
let inputData = null;   // Float32Array reused across frames
let runEvery = 30;     // run inference every N frames (heavy model)
let inputName = '';
let outputName = '';

// ============================================
// Setup
// ============================================

function setup() {
    if (!sys.neural.isAvailable()) {
        statusLine = 'ONNX Runtime not available (build without ENABLE_NEURAL?)';
        return;
    }

    try {
        modelId = sys.neural.loadModel('squeezenet1.1-7.onnx');
    } catch (e) {
        statusLine = 'Load failed: ' + e.message;
        return;
    }

    modelInfo = sys.neural.getModelInfo(modelId);
    inputName = modelInfo.inputs[0].name;
    outputName = modelInfo.outputs[0].name;
    statusLine = 'Model loaded OK';

    // Allocate the reusable input buffer (1×3×224×224 float32).
    inputData = new Float32Array(inputPixels);
}

// ============================================
// Update input data with a simple gradient pattern
// ============================================

function fillInputData(t) {
    // Generate a time-varying gradient that exercises all channels.
    const phase = (t * 0.001) % 1.0;
    for (let c = 0; c < 3; c++) {
        const offset = c * 224 * 224;
        for (let y = 0; y < 224; y++) {
            for (let x = 0; x < 224; x++) {
                inputData[offset + y * 224 + x] =
                    ((x / 224 + phase + c * 0.33) % 1.0);
            }
        }
    }
}

// ============================================
// Run inference and update top-1 result
// ============================================

function runInference(timestamp) {
    if (modelId < 0 || !inputData) return;

    fillInputData(timestamp);

    const t0 = Date.now();
    let result;
    try {
        result = sys.neural.run(modelId, { [inputName]: inputData });
    } catch (e) {
        statusLine = 'Inference error: ' + e.message;
        return;
    }
    inferenceMs = Date.now() - t0;

    const output = result[outputName];
    if (!output) {
        statusLine = 'Missing output tensor: ' + outputName;
        return;
    }

    // Find top-1 class index.
    let maxIdx = 0;
    for (let i = 1; i < output.length; i++) {
        if (output[i] > output[maxIdx]) maxIdx = i;
    }
    topIdx = maxIdx;
    topConf = output[maxIdx];
}

// ============================================
// Draw helpers
// ============================================

function drawPanel(x, y, w, h, title) {
    sys.canvas.setFillColor(COLORS.panel);
    sys.canvas.drawRoundRect(x, y, w, h, 8, 8);
    sys.canvas.setFillColor(COLORS.border);
    sys.canvas.drawRoundRect(x, y, w, 28, 8, 8);   // title bar
    sys.canvas.setFillColor(COLORS.accent);
    sys.canvas.drawText(title, x + 10, y + 19, 13);
}

function drawKV(label, value, x, y, labelColor, valueColor) {
    sys.canvas.setFillColor(labelColor || COLORS.dim);
    sys.canvas.drawText(label, x, y, 12);
    sys.canvas.setFillColor(valueColor || COLORS.text);
    sys.canvas.drawText(String(value), x + 180, y, 12);
}

// ============================================
// Frame loop
// ============================================

function frame(timestamp) {
    const W = sys.window.getWidth();
    const H = sys.window.getHeight();

    // FPS counter
    frameCount++;
    if (timestamp - lastFpsTime >= 1000) {
        fps = Math.round(frameCount * 1000 / (timestamp - lastFpsTime));
        frameCount = 0;
        lastFpsTime = timestamp;
    }

    // Run inference periodically so the UI stays responsive.
    if (modelId >= 0 && frameCount % runEvery === 0) {
        runInference(timestamp);
    }

    // ---- Background ----
    sys.canvas.clear(COLORS.bg);

    // ---- Title ----
    sys.canvas.setFillColor(COLORS.text);
    sys.canvas.drawText('sys.neural  —  SqueezeNet 1.1 demo', 20, 38, 22);
    sys.canvas.setFillColor(COLORS.dim);
    sys.canvas.drawText('fps: ' + fps, W - 80, 38, 13);

    // ---- Status panel ----
    drawPanel(20, 55, W - 40, 36, 'Status');
    sys.canvas.setFillColor(COLORS.warn);
    sys.canvas.drawText(statusLine, 30, 80, 13);

    // ---- Model info panel ----
    const panelX = 20;
    const panelW = Math.floor((W - 50) / 2);
    drawPanel(panelX, 105, panelW, 170, 'Model info');
    if (modelInfo) {
        const lx = panelX + 10;
        let ly = 138;
        const step = 18;
        drawKV('Producer', modelInfo.producerName || '—', lx, ly); ly += step;
        drawKV('Graph', modelInfo.graphName || '—', lx, ly); ly += step;
        drawKV('Domain', modelInfo.domain || '—', lx, ly); ly += step;
        drawKV('Version', modelInfo.version, lx, ly); ly += step;
        drawKV('Inputs', modelInfo.inputs.length, lx, ly); ly += step;
        drawKV('Outputs', modelInfo.outputs.length, lx, ly); ly += step;
        const inp = modelInfo.inputs[0];
        drawKV('Input name', inp.name, lx, ly); ly += step;
        drawKV('Input shape', JSON.stringify(inp.shape), lx, ly); ly += step;
        drawKV('Input dtype', inp.dtype, lx, ly);
    } else {
        sys.canvas.setFillColor(COLORS.dim);
        sys.canvas.drawText('No model loaded', panelX + 10, 138, 13);
    }

    // ---- Inference result panel ----
    const rx = 20 + panelW + 10;
    const rw = W - 40 - panelW - 10;
    drawPanel(rx, 105, rw, 170, 'Inference result');
    {
        const lx = rx + 10;
        let ly = 138;
        const step = 18;
        drawKV('Latency', inferenceMs > 0 ? inferenceMs.toFixed(1) + ' ms' : '—', lx, ly); ly += step;
        drawKV('Top-1 idx', topIdx >= 0 ? topIdx : '—', lx, ly); ly += step;
        drawKV('Confidence', topConf > 0 ? (topConf * 100).toFixed(2) + ' %' : '—', lx, ly); ly += step;
        drawKV('Run every', runEvery + ' frames', lx, ly); ly += step;

        if (topIdx >= 0) {
            sys.canvas.setFillColor(COLORS.success);
            sys.canvas.drawText(
                'Class #' + topIdx + ' at ' + (topConf * 100).toFixed(1) + '%',
                lx, ly + step, 16);

            // Confidence bar
            const bx = lx;
            const by = ly + step * 2 + 5;
            const bw = rw - 20;
            const bh = 14;
            sys.canvas.setFillColor(COLORS.border);
            sys.canvas.drawRect(bx, by, bw, bh);
            sys.canvas.setFillColor(COLORS.accent);
            sys.canvas.drawRect(bx, by, Math.floor(bw * Math.min(topConf, 1)), bh);
        }
    }

    // ---- Tensor I/O summary panel ----
    if (modelInfo) {
        drawPanel(20, 290, W - 40, 60 + modelInfo.inputs.length * 18 + modelInfo.outputs.length * 18, 'Tensor schema');
        let ly = 320;
        const lx = 30;
        sys.canvas.setFillColor(COLORS.dim);
        sys.canvas.drawText('Inputs:', lx, ly, 12); ly += 18;
        for (const t of modelInfo.inputs) {
            sys.canvas.setFillColor(COLORS.text);
            sys.canvas.drawText(
                '  ' + t.name + '  ' + t.dtype + '  ' + JSON.stringify(t.shape),
                lx, ly, 12);
            ly += 18;
        }
        sys.canvas.setFillColor(COLORS.dim);
        sys.canvas.drawText('Outputs:', lx, ly, 12); ly += 18;
        for (const t of modelInfo.outputs) {
            sys.canvas.setFillColor(COLORS.text);
            sys.canvas.drawText(
                '  ' + t.name + '  ' + t.dtype + '  ' + JSON.stringify(t.shape),
                lx, ly, 12);
            ly += 18;
        }
    }

    sys.animation.requestFrame(frame);
}

// ============================================
// Entry point
// ============================================

setup();
sys.animation.requestFrame(frame);
