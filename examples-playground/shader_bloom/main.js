// Bloom / Glow Shader Demo
// Neon-colored shapes with a bloom post-processing pass.
// Bright areas glow outward.

const shaderProgramId = sys.gl.createProgram('shader.vert', 'shader.frag');

let time = 0;

function frame(timestamp) {
    const width = sys.window.getWidth();
    const height = sys.window.getHeight();
    const input = sys.input.get();

    time += input.deltaTime;

    sys.canvas.clear('#050510');

    const cx = width / 2;
    const cy = height / 2;

    // Orbiting neon circles
    const numOrbs = 7;
    for (let i = 0; i < numOrbs; i++) {
        const angle = (i / numOrbs) * Math.PI * 2 + time * (0.5 + i * 0.15);
        const dist = 100 + 40 * Math.sin(time * 0.8 + i);
        const x = cx + Math.cos(angle) * dist;
        const y = cy + Math.sin(angle) * dist;
        const r = 16 + 6 * Math.sin(time * 2 + i * 1.5);

        const colors = ['#FF00FF', '#00FFFF', '#FFFF00', '#FF4488', '#44FF88', '#8844FF', '#FF8800'];
        sys.canvas.setFillColor(colors[i]);
        sys.canvas.drawCircle(x, y, r);
    }

    // Central pulsing ring
    const pulseR = 55 + 15 * Math.sin(time * 3);
    sys.canvas.setStrokeColor('#FFFFFF');
    sys.canvas.setStrokeWidth(4);
    sys.canvas.drawArc(cx - pulseR, cy - pulseR, pulseR * 2, pulseR * 2, 0, 360, false);

    // Rotating neon lines
    sys.canvas.save();
    sys.canvas.translate(cx, cy);
    for (let i = 0; i < 6; i++) {
        sys.canvas.rotate(30);
        const lineColors = ['#FF0066', '#00FF99', '#3366FF', '#FF9900', '#CC00FF', '#00FFCC'];
        sys.canvas.setStrokeColor(lineColors[i]);
        sys.canvas.setStrokeWidth(3);
        const len = 140 + 30 * Math.sin(time * 1.5 + i);
        sys.canvas.drawLine(0, 0, len, 0);
    }
    sys.canvas.restore();

    // Title
    sys.canvas.setFillColor('#FFFFFF');
    sys.canvas.drawText('Bloom Shader', 20, 32, 22);

    // Apply bloom
    sys.gl.setUniform1f(shaderProgramId, 'u_bloom_radius', 6.0);
    sys.gl.setUniform1f(shaderProgramId, 'u_bloom_intensity', 2.5);
    sys.gl.bindScreen();
    sys.gl.drawFullscreen(shaderProgramId);

    sys.animation.requestFrame(frame);
}

sys.animation.requestFrame(frame);
