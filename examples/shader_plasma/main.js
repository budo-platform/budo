// Plasma Shader Demo
// Self-similar fractal field blended over animated Skia shapes.
// Drag to pan, use the mouse wheel or pinch to zoom.

const shaderProgram = sys.gl.createProgram('shader.vert', 'shader.frag');

const WHEEL_ZOOM_STEP = 1.06;
const PINCH_ZOOM_POWER = 0.72;
const PAN_SPEED = 0.68;
const VIEW_SMOOTHNESS = 10.0;
const MOBILE_PINCH_ZOOM_POWER = 0.17;
const MOBILE_PAN_SPEED = 2.02;

let angle = 0;
let fractalCenterX = 0;
let fractalCenterY = 0;
let fractalScale = 1.35;
let fractalZoomPhase = 0;
let targetFractalCenterX = 0;
let targetFractalCenterY = 0;
let targetFractalScale = 1.35;
let targetFractalZoomPhase = 0;
let dragPointerId = null;
let dragPointerType = null;
let pinchDistance = 0;
let pinching = false;

function normalizeFractalViewState(scale, centerX, centerY) {
    while (scale < 0.75) {
        scale *= 3.0;
        centerX *= 3.0;
        centerY *= 3.0;
    }

    while (scale > 2.25) {
        scale /= 3.0;
        centerX /= 3.0;
        centerY /= 3.0;
    }

    return { scale, centerX, centerY };
}

function zoomFractal(factor) {
    if (!(factor > 0)) {
        return;
    }

    targetFractalZoomPhase -= Math.log(factor) / Math.log(3.0);
    targetFractalScale /= factor;

    const normalized = normalizeFractalViewState(targetFractalScale, targetFractalCenterX, targetFractalCenterY);
    targetFractalScale = normalized.scale;
    targetFractalCenterX = normalized.centerX;
    targetFractalCenterY = normalized.centerY;
}

function updateFractalView(input, width, height) {
    const displayDensity = sys.window.getDisplayDensity();
    const isMobile = displayDensity > 1.0;
    const pointers = Array.isArray(input.pointers) ? input.pointers : [];
    const pointer = input.pointer;
    const isDragging = pointer && pointer.down;
    const isNewDrag = !isDragging || pointer.pressed || pointer.id !== dragPointerId || pointer.type !== dragPointerType;
    const panSpeed = isMobile ? MOBILE_PAN_SPEED : PAN_SPEED;
    const pinchZoomPower = isMobile ? MOBILE_PINCH_ZOOM_POWER : PINCH_ZOOM_POWER;
    const pixelsToWorld = (2 * targetFractalScale * panSpeed) / Math.max(height, 1);

    if (input.mouse.wheelY) {
        zoomFractal(Math.pow(WHEEL_ZOOM_STEP, input.mouse.wheelY));
    }

    if (pointers.length >= 2) {
        const dx = pointers[1].x - pointers[0].x;
        const dy = pointers[1].y - pointers[0].y;
        const distance = Math.sqrt(dx * dx + dy * dy);

        if (pinching && pinchDistance > 0) {
            zoomFractal(Math.pow(distance / pinchDistance, pinchZoomPower));
        }

        pinchDistance = distance;
        pinching = true;
        dragPointerId = null;
        dragPointerType = null;
        return;
    }

    pinching = false;
    pinchDistance = 0;

    if (isDragging) {
        if (isNewDrag) {
            dragPointerId = pointer.id;
            dragPointerType = pointer.type;
        } else {
            targetFractalCenterX -= pointer.dx * pixelsToWorld;
            targetFractalCenterY += pointer.dy * pixelsToWorld;
        }
    } else {
        dragPointerId = null;
        dragPointerType = null;
    }

    const normalized = normalizeFractalViewState(targetFractalScale, targetFractalCenterX, targetFractalCenterY);
    targetFractalScale = normalized.scale;
    targetFractalCenterX = normalized.centerX;
    targetFractalCenterY = normalized.centerY;
}

function smoothFractalView(deltaTime) {
    const blend = 1.0 - Math.exp(-Math.max(deltaTime, 0) * VIEW_SMOOTHNESS);

    fractalCenterX += (targetFractalCenterX - fractalCenterX) * blend;
    fractalCenterY += (targetFractalCenterY - fractalCenterY) * blend;
    fractalScale += (targetFractalScale - fractalScale) * blend;
    fractalZoomPhase += (targetFractalZoomPhase - fractalZoomPhase) * blend;

    const normalized = normalizeFractalViewState(fractalScale, fractalCenterX, fractalCenterY);
    fractalScale = normalized.scale;
    fractalCenterX = normalized.centerX;
    fractalCenterY = normalized.centerY;
}

function frame(timestamp) {
    const width = sys.window.getWidth();
    const height = sys.window.getHeight();
    const displayDensity = sys.window.getDisplayDensity();
    const isMobile = displayDensity > 1.0;
    const input = sys.input.get();

    angle += input.deltaTime * 0.5;
    updateFractalView(input, width, height);
    smoothFractalView(input.deltaTime);

    sys.canvas.clear('#1A1A2E');

    // Draw a grid of softly colored circles
    const cols = isMobile ? 6 : 8;
    const rows = isMobile ? 4 : 6;
    const spacingX = width / (cols + 1);
    const spacingY = height / (rows + 1);

    for (let r = 0; r < rows; r++) {
        for (let c = 0; c < cols; c++) {
            const x = spacingX * (c + 1);
            const y = spacingY * (r + 1);
            const hue = ((r * cols + c) * 15 + angle * 40) % 360;
            const radius = 18 + 8 * Math.sin(angle * 2 + r + c);

            sys.canvas.setFillColor(hslToHex(hue, 70, 55));
            sys.canvas.drawCircle(x, y, radius);
        }
    }

    sys.canvas.setFillColor('#FFFFFF');
    sys.canvas.drawText('Fractal Plasma', 20, 32, 22);
    sys.canvas.drawText('Drag to pan - wheel or pinch to zoom', 20, 56, 16);

    // Apply fractal shader with persistent pan and normalized infinite zoom.
    sys.gl.setUniform2f(shaderProgram, 'u_center', fractalCenterX, fractalCenterY);
    sys.gl.setUniform1f(shaderProgram, 'u_scale', fractalScale);
    sys.gl.setUniform1f(shaderProgram, 'u_zoomPhase', fractalZoomPhase);
    sys.gl.setUniform1f(shaderProgram, 'u_mobile', isMobile ? 1.0 : 0.0);
    sys.gl.bindScreen();
    sys.gl.drawFullscreen(shaderProgram);

    sys.animation.requestFrame(frame);
}

function hslToHex(h, s, l) {
    s /= 100;
    l /= 100;
    const k = (n) => (n + h / 30) % 12;
    const a = s * Math.min(l, 1 - l);
    const f = (n) => l - a * Math.max(-1, Math.min(k(n) - 3, 9 - k(n), 1));
    const r = Math.round(f(0) * 255);
    const g = Math.round(f(8) * 255);
    const b = Math.round(f(4) * 255);
    return '#' + ((1 << 24) + (r << 16) + (g << 8) + b).toString(16).slice(1);
}

sys.animation.requestFrame(frame);
