import { drawCenteredCircle, drawLabel, COLORS } from './utils.js';

function frame(timestamp) {
    const width = sys.window.getWidth();
    const height = sys.window.getHeight();

    sys.canvas.clear(COLORS.bg);

    // Animated circle using imported helper
    const t = timestamp / 1000;
    const cx = width / 2 + Math.sin(t) * 100;
    const cy = height / 2 + Math.cos(t) * 50;

    drawCenteredCircle(cx, cy, 40, COLORS.primary);
    drawCenteredCircle(cx + 60, cy + 30, 20, COLORS.accent);

    drawLabel('ES Module Demo', 20, 30, COLORS.primary);
    drawLabel('Imported from utils.js', 20, 55, COLORS.light);

    sys.animation.requestFrame(frame);
}

sys.animation.requestFrame(frame);
