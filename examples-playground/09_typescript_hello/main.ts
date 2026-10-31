// TypeScript example for Budo
// Demonstrates type annotations being stripped at load time

interface Point {
    x: number;
    y: number;
}

type Color = string;

const BACKGROUND: Color = '#1a1a2e';
const BALL_COLOR: Color = '#e94560';

let ballX: number = 400;
let ballY: number = 300;
let dx: number = 3;
let dy: number = 2;
const radius: number = 20;

function clamp(value: number, min: number, max: number): number {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

function updateBall(width: number, height: number): void {
    ballX += dx;
    ballY += dy;

    if (ballX - radius <= 0 || ballX + radius >= width) {
        dx = -dx;
        ballX = clamp(ballX, radius, width - radius);
    }
    if (ballY - radius <= 0 || ballY + radius >= height) {
        dy = -dy;
        ballY = clamp(ballY, radius, height - radius);
    }
}

function drawBall(pos: Point): void {
    sys.canvas.setFillColor(BALL_COLOR);
    sys.canvas.drawCircle(pos.x, pos.y, radius);
}

function frame(timestamp: number): void {
    const w: number = sys.window.getWidth();
    const h: number = sys.window.getHeight();

    updateBall(w, h);

    sys.canvas.clear(BACKGROUND);

    // Draw trail
    sys.canvas.setAlpha(80);
    sys.canvas.setFillColor('#16213e');
    sys.canvas.drawCircle(ballX - dx * 3, ballY - dy * 3, radius * 0.7);
    sys.canvas.drawCircle(ballX - dx * 6, ballY - dy * 6, radius * 0.4);
    sys.canvas.setAlpha(255);

    const pos: Point = { x: ballX, y: ballY };
    drawBall(pos);

    // HUD
    sys.canvas.setFillColor('#ffffff');
    sys.canvas.drawText('TypeScript Bouncing Ball', 10, 30, 20);
    sys.canvas.drawText('x: ' + Math.round(ballX) + ' y: ' + Math.round(ballY), 10, 55, 14);

    sys.animation.requestFrame(frame);
}

sys.animation.requestFrame(frame);
