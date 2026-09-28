const STEPS = [
    { label: 'START', instruction: 'Touch to start', background: '#DFF7E2' },
    { label: 'STOP', instruction: 'Touch to stop', background: '#DDEBFF' },
    { label: 'RESET', instruction: 'Touch to reset', background: '#FFE5D9' },
];

const FONT_SIZES = {
    label: 30,
    time: 42,
    instruction: 18,
};

const TEXT_Y = {
    label: 0.28,
    time: 0.52,
    instruction: 0.72,
};

const TIME_TEMPLATE = '00:00.00';
const WATCH_PADDING = 32;
const WATCH_STROKE_WIDTH = 5;

// Text does not change shape during the demo, so measure it once at startup.
const TEXT_SIZES = {
    steps: STEPS.map((step) => ({
        label: sys.canvas.measureTextRect(step.label, FONT_SIZES.label),
        instruction: sys.canvas.measureTextRect(step.instruction, FONT_SIZES.instruction),
    })),
    time: sys.canvas.measureTextRect(TIME_TEMPLATE, FONT_SIZES.time),
};

let stepIndex = 0;
let startTime = 0;
let elapsedTime = 0;

function formatTime(ms) {
    const hundredths = Math.floor(ms / 10);
    const minutes = Math.floor(hundredths / 6000);
    const seconds = Math.floor(hundredths / 100) % 60;
    const remainingHundredths = hundredths % 100;

    return minutes.toString().padStart(2, '0') + ':'
        + seconds.toString().padStart(2, '0') + '.'
        + remainingHundredths.toString().padStart(2, '0');
}

function isPressStarted(input) {
    return !!((input.pointer && input.pointer.pressed) || input.mouse.leftPressed);
}

function advanceStep(timestamp) {
    if (stepIndex === 0) {
        stepIndex = 1;
        startTime = timestamp;
        return;
    }

    if (stepIndex === 1) {
        stepIndex = 2;
        elapsedTime = timestamp - startTime;
        return;
    }

    stepIndex = 0;
    startTime = 0;
    elapsedTime = 0;
}

function updateElapsedTime(timestamp) {
    if (stepIndex === 1) {
        elapsedTime = timestamp - startTime;
    }
}

function getCenteredLine(width, height, size, yRatio) {
    const centerX = width * 0.5;
    const baselineY = height * yRatio;
    const x = centerX - size.width * 0.5;

    return {
        x,
        y: baselineY,
        left: x,
        top: baselineY - size.height,
        right: x + size.width,
        bottom: baselineY,
    };
}

// The watch outline is sized from the combined bounds of all visible text.
function getTextLayout(width, height) {
    const sizes = TEXT_SIZES.steps[stepIndex];
    const lines = {
        label: getCenteredLine(width, height, sizes.label, TEXT_Y.label),
        time: getCenteredLine(width, height, TEXT_SIZES.time, TEXT_Y.time),
        instruction: getCenteredLine(width, height, sizes.instruction, TEXT_Y.instruction),
    };

    const left = Math.min(lines.label.left, lines.time.left, lines.instruction.left);
    const top = Math.min(lines.label.top, lines.time.top, lines.instruction.top);
    const right = Math.max(lines.label.right, lines.time.right, lines.instruction.right);
    const bottom = Math.max(lines.label.bottom, lines.time.bottom, lines.instruction.bottom);
    const blockWidth = right - left;
    const blockHeight = bottom - top;

    return {
        lines,
        watch: {
            centerX: (left + right) * 0.5,
            centerY: (top + bottom) * 0.5,
            radius: Math.sqrt(blockWidth * blockWidth + blockHeight * blockHeight) * 0.5 + WATCH_PADDING,
        },
    };
}

function drawWatchOutline(watch) {
    sys.canvas.setStrokeColor('#1D1D1D55');
    sys.canvas.setStrokeWidth(WATCH_STROKE_WIDTH);
    sys.canvas.drawCircle(watch.centerX, watch.centerY, watch.radius);
}

function drawChronometerText(step, layout) {
    sys.canvas.setFillColor('#1D1D1D');
    sys.canvas.drawText(step.label, layout.lines.label.x, layout.lines.label.y, FONT_SIZES.label);
    sys.canvas.drawText(formatTime(elapsedTime), layout.lines.time.x, layout.lines.time.y, FONT_SIZES.time);
    sys.canvas.drawText(step.instruction, layout.lines.instruction.x, layout.lines.instruction.y, FONT_SIZES.instruction);
}

function frame(timestamp) {
    const width = sys.window.getWidth();
    const height = sys.window.getHeight();
    const input = sys.input.get();

    if (isPressStarted(input)) {
        advanceStep(timestamp);
    }

    updateElapsedTime(timestamp);

    const currentStep = STEPS[stepIndex];
    const layout = getTextLayout(width, height);

    sys.canvas.clear(currentStep.background);
    drawWatchOutline(layout.watch);
    drawChronometerText(currentStep, layout);

    sys.animation.requestFrame(frame);
}

sys.animation.requestFrame(frame);