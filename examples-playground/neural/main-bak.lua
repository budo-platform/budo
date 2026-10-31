--[[
    Neural Demo — Lua

    Demonstrates the sys.neural API using a pre-quantized SqueezeNet 1.1 model
    (squeezenet1.1-7.onnx, ~1.2 MB) from the ONNX Model Zoo.

    Expected model: squeezenet1.1-7.onnx in the same directory as this file.
    Input:  "data"          float32 [1, 3, 224, 224]
    Output: "softmaxout_1"  float32 [1, 1000]

    See README.md for download instructions.
]]

-- ============================================
-- Constants / colors
-- ============================================

local COLORS = {
    bg      = "#1a1a2e",
    panel   = "#16213e",
    border  = "#0f3460",
    accent  = "#e94560",
    text    = "#e0e0e0",
    dim     = "#888888",
    success = "#4ecdc4",
    warn    = "#ffeaa7",
}

-- ============================================
-- State
-- ============================================

local model_id     = -1
local model_info   = nil
local status_line  = "Initializing..."
local inference_ms = 0
local top_idx      = -1
local top_conf     = 0.0
local frame_count  = 0
local last_fps_t   = 0
local fps          = 0
local input_name   = ""
local output_name  = ""
local run_every    = 30   -- run inference once per N frames

-- Pre-built flat input table (1-based, 1×3×224×224 = 150528 floats).
local INPUT_SIZE = 1 * 3 * 224 * 224
local input_data = {}
for i = 1, INPUT_SIZE do
    input_data[i] = 0
end

-- ============================================
-- Setup (called once before the frame loop)
-- ============================================

local function setup()
    if not sys.neural.isAvailable() then
        status_line = "ONNX Runtime not available (build without ENABLE_NEURAL?)"
        return
    end

    local ok, err = pcall(function()
        model_id = sys.neural.loadModel("squeezenet1.1-7.onnx")
    end)
    if not ok then
        status_line = "Load failed: " .. tostring(err)
        return
    end
    if model_id < 0 then
        status_line = "Load failed: " .. sys.neural.getError()
        return
    end

    model_info   = sys.neural.getModelInfo(model_id)
    input_name   = model_info.inputs[1].name
    output_name  = model_info.outputs[1].name
    status_line  = "Model loaded OK"
end

-- ============================================
-- Fill the input buffer with a time-varying gradient
-- ============================================

local function fill_input(t)
    local phase = (t * 0.001) % 1.0
    for c = 0, 2 do
        local offset = c * 224 * 224
        for y = 0, 223 do
            for x = 0, 223 do
                input_data[offset + y * 224 + x + 1] =
                    ((x / 224 + phase + c * 0.33) % 1.0)
            end
        end
    end
end

-- ============================================
-- Run inference and update top-1 result
-- ============================================

local function run_inference(timestamp)
    if model_id < 0 then return end

    fill_input(timestamp)

    local t0 = os.clock()
    local result, err = pcall(function()
        return sys.neural.run(model_id, {
            [input_name] = {
                data  = input_data,
                shape = { 1, 3, 224, 224 },
            }
        })
    end)
    inference_ms = math.floor((os.clock() - t0) * 1000)

    if not result then
        status_line = "Inference error: " .. tostring(err)
        return
    end

    -- err is the actual return value when pcall succeeded
    local outputs = err
    local out = outputs[output_name]
    if not out then
        status_line = "Missing output tensor: " .. output_name
        return
    end

    -- Find top-1 class.
    local max_val = out.data[1]
    local max_idx = 1
    for i = 2, #out.data do
        if out.data[i] > max_val then
            max_val = out.data[i]
            max_idx = i
        end
    end
    -- Convert 1-based Lua index to 0-based class index.
    top_idx  = max_idx - 1
    top_conf = max_val
end

-- ============================================
-- Draw helpers
-- ============================================

local function draw_panel(x, y, w, h, title)
    sys.canvas.setFillColor(COLORS.panel)
    sys.canvas.drawRoundRect(x, y, w, h, 8, 8)
    sys.canvas.setFillColor(COLORS.border)
    sys.canvas.drawRoundRect(x, y, w, 28, 8, 8)
    sys.canvas.setFillColor(COLORS.accent)
    sys.canvas.drawText(title, x + 10, y + 19, 13)
end

local function draw_kv(label, value, x, y, lc, vc)
    sys.canvas.setFillColor(lc or COLORS.dim)
    sys.canvas.drawText(label, x, y, 12)
    sys.canvas.setFillColor(vc or COLORS.text)
    sys.canvas.drawText(tostring(value), x + 180, y, 12)
end

local function shape_str(shape)
    local parts = {}
    for _, v in ipairs(shape) do
        parts[#parts + 1] = tostring(v)
    end
    return "[" .. table.concat(parts, ", ") .. "]"
end

-- ============================================
-- Frame loop
-- ============================================

sys.animation.start(function(timestamp)
    local W = sys.window.getWidth()
    local H = sys.window.getHeight()

    -- FPS counter
    frame_count = frame_count + 1
    if timestamp - last_fps_t >= 1000 then
        fps = math.floor(frame_count * 1000 / math.max(timestamp - last_fps_t, 1))
        frame_count = 0
        last_fps_t  = timestamp
    end

    -- Run inference periodically to keep the UI responsive.
    if model_id >= 0 and frame_count % run_every == 0 then
        run_inference(timestamp)
    end

    -- ---- Background ----
    sys.canvas.clear(COLORS.bg)

    -- ---- Title ----
    sys.canvas.setFillColor(COLORS.text)
    sys.canvas.drawText("sys.neural  —  SqueezeNet 1.1 demo (Lua)", 20, 38, 20)
    sys.canvas.setFillColor(COLORS.dim)
    sys.canvas.drawText("fps: " .. fps, W - 80, 38, 13)

    -- ---- Status panel ----
    draw_panel(20, 55, W - 40, 36, "Status")
    sys.canvas.setFillColor(COLORS.warn)
    sys.canvas.drawText(status_line, 30, 80, 13)

    -- ---- Model info panel ----
    local panel_x = 20
    local panel_w = math.floor((W - 50) / 2)
    draw_panel(panel_x, 105, panel_w, 170, "Model info")
    if model_info then
        local lx, ly = panel_x + 10, 138
        local step = 18
        draw_kv("Producer",   model_info.producerName or "—", lx, ly); ly = ly + step
        draw_kv("Graph",      model_info.graphName    or "—", lx, ly); ly = ly + step
        draw_kv("Domain",     model_info.domain       or "—", lx, ly); ly = ly + step
        draw_kv("Version",    tostring(model_info.version),   lx, ly); ly = ly + step
        draw_kv("Inputs",     tostring(#model_info.inputs),   lx, ly); ly = ly + step
        draw_kv("Outputs",    tostring(#model_info.outputs),  lx, ly); ly = ly + step
        local inp = model_info.inputs[1]
        draw_kv("Input name", inp.name,                        lx, ly); ly = ly + step
        draw_kv("Input shape",shape_str(inp.shape),            lx, ly); ly = ly + step
        draw_kv("Input dtype",inp.dtype,                       lx, ly)
    else
        sys.canvas.setFillColor(COLORS.dim)
        sys.canvas.drawText("No model loaded", panel_x + 10, 138, 13)
    end

    -- ---- Inference result panel ----
    local rx = 20 + panel_w + 10
    local rw = W - 40 - panel_w - 10
    draw_panel(rx, 105, rw, 170, "Inference result")
    do
        local lx, ly = rx + 10, 138
        local step = 18
        local lat = inference_ms > 0 and (inference_ms .. " ms") or "—"
        draw_kv("Latency",    lat,                                               lx, ly); ly = ly + step
        draw_kv("Top-1 idx",  top_idx >= 0 and tostring(top_idx) or "—",        lx, ly); ly = ly + step
        local conf = top_conf > 0 and string.format("%.2f%%", top_conf * 100) or "—"
        draw_kv("Confidence", conf,                                              lx, ly); ly = ly + step
        draw_kv("Run every",  run_every .. " frames",                            lx, ly); ly = ly + step

        if top_idx >= 0 then
            sys.canvas.setFillColor(COLORS.success)
            sys.canvas.drawText(
                string.format("Class #%d at %.1f%%", top_idx, top_conf * 100),
                lx, ly + step, 16)

            -- Confidence bar
            local bx = lx
            local by = ly + step * 2 + 5
            local bw = rw - 20
            local bh = 14
            sys.canvas.setFillColor(COLORS.border)
            sys.canvas.drawRect(bx, by, bw, bh)
            sys.canvas.setFillColor(COLORS.accent)
            sys.canvas.drawRect(bx, by, math.floor(bw * math.min(top_conf, 1)), bh)
        end
    end

    -- ---- Tensor schema panel ----
    if model_info then
        local row_h = #model_info.inputs * 18 + #model_info.outputs * 18
        draw_panel(20, 290, W - 40, 60 + row_h, "Tensor schema")
        local lx, ly = 30, 320
        sys.canvas.setFillColor(COLORS.dim)
        sys.canvas.drawText("Inputs:", lx, ly, 12); ly = ly + 18
        for _, t in ipairs(model_info.inputs) do
            sys.canvas.setFillColor(COLORS.text)
            sys.canvas.drawText(
                "  " .. t.name .. "  " .. t.dtype .. "  " .. shape_str(t.shape),
                lx, ly, 12)
            ly = ly + 18
        end
        sys.canvas.setFillColor(COLORS.dim)
        sys.canvas.drawText("Outputs:", lx, ly, 12); ly = ly + 18
        for _, t in ipairs(model_info.outputs) do
            sys.canvas.setFillColor(COLORS.text)
            sys.canvas.drawText(
                "  " .. t.name .. "  " .. t.dtype .. "  " .. shape_str(t.shape),
                lx, ly, 12)
            ly = ly + 18
        end
    end
end)

-- ============================================
-- Entry point
-- ============================================

setup()
