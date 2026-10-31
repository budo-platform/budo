// CRT / Retro TV Shader Demo
// Applies scanlines, barrel distortion, chromatic aberration, and vignette
// over an animated retro-style scene.

const shaderProgramId = sys.gl.createProgram('shader.vert', 'shader.frag');

let time = 0;
const balls = [];

// Create bouncing balls
for (let i = 0; i < 6; i++) {
    balls.push({
        x: 100 + i * 110,
        y: 150 + (i % 3) * 80,
        vx: (60 + Math.random() * 80) * (i % 2 === 0 ? 1 : -1),
        vy: (40 + Math.random() * 60) * (i % 2 === 0 ? -1 : 1),
        radius: 20 + i * 5,
        color: ['#FF6B6B', '#4ECDC4', '#45B7D1', '#FFEAA7', '#DDA0DD', '#98D8C8'][i]
    });
}

function frame(timestamp) {
    const width = sys.window.getWidth();
    const height = sys.window.getHeight();
    const input = sys.input.get();
    const dt = input.deltaTime;

    time += dt;

    sys.canvas.clear('#0A0A1A');

    // Draw a retro grid floor
    sys.canvas.setStrokeColor('#1A3A2A');
    sys.canvas.setStrokeWidth(1);
    const gridSize = 40;
    for (let x = 0; x < width; x += gridSize) {
        sys.canvas.drawLine(x, 0, x, height);
    }
    for (let y = 0; y < height; y += gridSize) {
        sys.canvas.drawLine(0, y, width, y);
    }

    // Animate and draw bouncing balls
    for (let i = 0; i < balls.length; i++) {
        const b = balls[i];
        b.x += b.vx * dt;
        b.y += b.vy * dt;

        if (b.x - b.radius < 0 || b.x + b.radius > width) b.vx *= -1;
        if (b.y - b.radius < 0 || b.y + b.radius > height) b.vy *= -1;

        b.x = Math.max(b.radius, Math.min(width - b.radius, b.x));
        b.y = Math.max(b.radius, Math.min(height - b.radius, b.y));

        sys.canvas.setFillColor(b.color);
        sys.canvas.drawCircle(b.x, b.y, b.radius);

        // Shadow/trail
        sys.canvas.setAlpha(80);
        sys.canvas.drawCircle(b.x - b.vx * 0.05, b.y - b.vy * 0.05, b.radius * 0.8);
        sys.canvas.setAlpha(255);
    }

    // Score-like text
    sys.canvas.setFillColor('#33FF33');
    sys.canvas.drawText('PLAYER 1', 30, 40, 28);
    sys.canvas.drawText(String(Math.floor(time * 100)), 30, 70, 22);

    sys.canvas.setFillColor('#FF3333');
    sys.canvas.drawText('PLAYER 2', width - 180, 40, 28);
    sys.canvas.drawText(String(Math.floor(time * 77)), width - 180, 70, 22);

    // Apply CRT shader
    sys.gl.setUniform1f(shaderProgramId, 'u_scanline_intensity', 0.25);
    sys.gl.setUniform1f(shaderProgramId, 'u_aberration', 1.0);
    sys.gl.bindScreen();
    sys.gl.drawFullscreen(shaderProgramId);

    sys.animation.requestFrame(frame);
}

sys.animation.requestFrame(frame);
