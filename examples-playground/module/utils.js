// Utility module demonstrating ES module exports

export function drawCenteredCircle(x, y, radius, color) {
    sys.canvas.setFillColor(color);
    sys.canvas.drawCircle(x, y, radius);
}

export function drawLabel(text, x, y, color) {
    sys.canvas.setFillColor(color);
    sys.canvas.drawText(text, x, y, 20);
}

export const COLORS = {
    bg: '#F0F0F0',
    primary: '#0F4C5C',
    accent: '#E36414',
    light: '#9A8C98',
};
