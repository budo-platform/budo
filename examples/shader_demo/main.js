const shaderProgram = sys.gl.createProgram('shader.vert', 'shader.frag');

let angle = 0;

function frame(timestamp) {
    const width = sys.window.getWidth();
    const height = sys.window.getHeight();
    const input = sys.input.get();
    const cx = width * 0.5;
    const cy = height * 0.5;

    angle += input.deltaTime;

    sys.canvas.clear('#F5EFE6');

    sys.canvas.save();
    sys.canvas.translate(cx, cy);
    sys.canvas.rotate(angle * 35.0);

    sys.canvas.setFillColor('#0F4C5C');
    sys.canvas.drawRoundRect(-180, -110, 360, 220, 28, 28);

    sys.canvas.setFillColor('#E36414');
    sys.canvas.drawCircle(0, 0, 72);

    sys.canvas.setStrokeColor('#FB8B24');
    sys.canvas.setStrokeWidth(10);
    sys.canvas.drawArc(-140, -140, 280, 280, 0, 280, false);

    sys.canvas.restore();

    sys.canvas.setFillColor('#1B1B1B');
    sys.canvas.drawText('Skia + OpenGL shader pass', 24, 42, 26);

    sys.gl.setUniform1f(shaderProgram, 'u_strength', 0.16);
    sys.gl.setUniform2f(shaderProgram, 'u_mouse', input.mouse.x, input.mouse.y);
    sys.gl.bindScreen();
    sys.gl.drawFullscreen(shaderProgram);

    sys.animation.requestFrame(frame);
}

sys.animation.requestFrame(frame);