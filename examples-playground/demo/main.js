/**
 * Budo Demo
 * 
 * This example demonstrates the canvas API with animated graphics
 * and interactive mouse/keyboard input.
 */

// Animation state
let time = 0;
let mouseTrail = [];
const MAX_TRAIL_LENGTH = 50;

// Colors
const COLORS = [
    '#FF6B6B',  // Red
    '#4ECDC4',  // Teal
    '#45B7D1',  // Blue
    '#96CEB4',  // Green
    '#FFEAA7',  // Yellow
    '#DDA0DD',  // Plum
    '#98D8C8',  // Mint
    '#F7DC6F',  // Gold
];

// Helper functions
function lerp(a, b, t) {
    return a + (b - a) * t;
}

function hslToRgb(h, s, l) {
    let r, g, b;
    if (s === 0) {
        r = g = b = l;
    } else {
        const hue2rgb = (p, q, t) => {
            if (t < 0) t += 1;
            if (t > 1) t -= 1;
            if (t < 1 / 6) return p + (q - p) * 6 * t;
            if (t < 1 / 2) return q;
            if (t < 2 / 3) return p + (q - p) * (2 / 3 - t) * 6;
            return p;
        };
        const q = l < 0.5 ? l * (1 + s) : l + s - l * s;
        const p = 2 * l - q;
        r = hue2rgb(p, q, h + 1 / 3);
        g = hue2rgb(p, q, h);
        b = hue2rgb(p, q, h - 1 / 3);
    }
    return [Math.round(r * 255), Math.round(g * 255), Math.round(b * 255)];
}

function rgbToHex(r, g, b) {
    return '#' + [r, g, b].map(x => {
        const hex = x.toString(16);
        return hex.length === 1 ? '0' + hex : hex;
    }).join('');
}

// Draw a rotating star
function drawStar(cx, cy, outerRadius, innerRadius, points, rotation) {
    const path = sys.path.create();
    const step = Math.PI / points;

    for (let i = 0; i < points * 2; i++) {
        const radius = i % 2 === 0 ? outerRadius : innerRadius;
        const angle = i * step + rotation;
        const x = cx + Math.cos(angle) * radius;
        const y = cy + Math.sin(angle) * radius;

        if (i === 0) {
            sys.path.moveTo(path, x, y);
        } else {
            sys.path.lineTo(path, x, y);
        }
    }
    sys.path.close(path);
    sys.canvas.drawPath(path);
}

// Draw bouncing balls
const balls = [];
for (let i = 0; i < 10; i++) {
    balls.push({
        x: Math.random() * 700 + 50,
        y: Math.random() * 500 + 50,
        vx: (Math.random() - 0.5) * 5,
        vy: (Math.random() - 0.5) * 5,
        radius: Math.random() * 20 + 10,
        color: COLORS[i % COLORS.length]
    });
}

function updateBalls(width, height, deltaTime) {
    const dt = deltaTime * 60; // Normalize to 60fps

    for (const ball of balls) {
        ball.x += ball.vx * dt;
        ball.y += ball.vy * dt;

        // Bounce off walls
        if (ball.x - ball.radius < 0) {
            ball.x = ball.radius;
            ball.vx = Math.abs(ball.vx);
        }
        if (ball.x + ball.radius > width) {
            ball.x = width - ball.radius;
            ball.vx = -Math.abs(ball.vx);
        }
        if (ball.y - ball.radius < 0) {
            ball.y = ball.radius;
            ball.vy = Math.abs(ball.vy);
        }
        if (ball.y + ball.radius > height) {
            ball.y = height - ball.radius;
            ball.vy = -Math.abs(ball.vy);
        }
    }
}

// Main animation frame
function animate(timestamp) {
    const width = sys.window.getWidth();
    const height = sys.window.getHeight();
    const input = sys.input.get();

    time = timestamp / 1000; // Convert to seconds

    // Update mouse trail
    if (input.mouse.left) {
        mouseTrail.push({ x: input.mouse.x, y: input.mouse.y, age: 0 });
        if (mouseTrail.length > MAX_TRAIL_LENGTH) {
            mouseTrail.shift();
        }
    }

    // Age trail points
    for (let i = mouseTrail.length - 1; i >= 0; i--) {
        mouseTrail[i].age += input.deltaTime;
        if (mouseTrail[i].age > 1) {
            mouseTrail.splice(i, 1);
        }
    }

    // Update balls
    updateBalls(width, height, input.deltaTime);

    // Clear with animated gradient background
    const bgHue = (time * 0.05) % 1;
    const [r, g, b] = hslToRgb(bgHue, 0.3, 0.95);
    sys.canvas.clear(rgbToHex(r, g, b));

    // Draw bouncing balls
    for (const ball of balls) {
        sys.canvas.setFillColor(ball.color);
        sys.canvas.drawCircle(ball.x, ball.y, ball.radius);

        // Draw ball shadow
        sys.canvas.setFillColor('#00000022');
        sys.canvas.drawOval(ball.x - ball.radius, height - 20, ball.radius * 2, 10);
    }

    // Draw mouse trail
    if (mouseTrail.length > 1) {
        for (let i = 1; i < mouseTrail.length; i++) {
            const p1 = mouseTrail[i - 1];
            const p2 = mouseTrail[i];
            const alpha = Math.floor((1 - p2.age) * 255);
            const hue = (time * 0.5 + i * 0.02) % 1;
            const [tr, tg, tb] = hslToRgb(hue, 0.8, 0.5);

            sys.canvas.setStrokeColor(rgbToHex(tr, tg, tb));
            sys.canvas.setStrokeWidth(10 * (1 - p2.age));
            sys.canvas.setAlpha(alpha);
            sys.canvas.drawLine(p1.x, p1.y, p2.x, p2.y);
            sys.canvas.setAlpha(255);
        }
    }

    // Draw rotating star in center
    sys.canvas.save();
    sys.canvas.translate(width / 2, height / 2);

    const starScale = 1 + Math.sin(time * 2) * 0.2;
    sys.canvas.scale(starScale, starScale);

    // Star with gradient effect
    for (let i = 5; i > 0; i--) {
        const hue = (time * 0.2 + i * 0.1) % 1;
        const [sr, sg, sb] = hslToRgb(hue, 0.7, 0.5);
        sys.canvas.setFillColor(rgbToHex(sr, sg, sb));
        sys.canvas.setAlpha(Math.floor(255 * (i / 5)));
        drawStar(0, 0, 40 + i * 5, 20 + i * 2, 5, time + i * 0.1);
    }
    sys.canvas.setAlpha(255);

    sys.canvas.restore();

    // Draw info text
    sys.canvas.setFillColor('#333333');
    sys.canvas.drawText('Budo Demo', 20, 30, 24);
    sys.canvas.drawText('Click and drag to draw a trail', 20, 55, 16);
    sys.canvas.drawText('Press ESC to exit', 20, 75, 14);

    // Draw FPS counter
    const fps = input.deltaTime > 0 ? Math.round(1 / input.deltaTime) : 0;
    sys.canvas.drawText(`FPS: ${fps}`, width - 80, 30, 16);

    // Draw mouse position
    sys.canvas.drawText(`Mouse: ${input.mouse.x}, ${input.mouse.y}`, width - 150, 50, 14);

    // Draw frame counter
    sys.canvas.drawText(`Frame: ${input.frameCount}`, width - 150, 70, 14);

    // Draw interactive circles that follow mouse
    if (!input.mouse.left) {
        const mx = input.mouse.x;
        const my = input.mouse.y;

        for (let i = 0; i < 5; i++) {
            const offset = i * 0.2;
            const x = mx + Math.cos(time * 3 + offset * Math.PI * 2) * (30 + i * 10);
            const y = my + Math.sin(time * 3 + offset * Math.PI * 2) * (30 + i * 10);

            sys.canvas.setFillColor(COLORS[i % COLORS.length]);
            sys.canvas.setAlpha(180);
            sys.canvas.drawCircle(x, y, 15 - i * 2);
        }
        sys.canvas.setAlpha(255);
    }

    // Draw a bezier curve
    sys.canvas.setStrokeColor('#9B59B6');
    sys.canvas.setStrokeWidth(3);

    const path = sys.path.create();
    const curveOffset = Math.sin(time) * 50;
    sys.path.moveTo(path, 50, height - 100);
    sys.path.cubicTo(path,
        150, height - 100 + curveOffset,
        250, height - 100 - curveOffset,
        350, height - 100
    );
    sys.canvas.drawPath(path);

    // Continue animation
    sys.animation.requestFrame(animate);
}

// Start the animation loop
sys.log('Starting Budo demo...');
sys.animation.requestFrame(animate);
