/// <reference path="../../budo.d.ts" />

const shader = sys.gl.createProgram('shader.vert', 'shader.frag');
const ui = sys.graphics.createCanvasTexture(sys.window.getWidth(), sys.window.getHeight());
ui.canvas.clear('#00000000');

function frame(timestamp) {
    const input = sys.input.get();

    if (sys.input.get().pointer.down) {
        ui.canvas.setFillColor('#f45e6a');
        ui.canvas.drawCircle(input.mouse.x, input.mouse.y, 20 * (1 + (input.pointer.dx + input.pointer.dy) / 10));
    }

    ui.canvas.setFillColor('#FFFFFF');
    ui.canvas.drawText('CanvasTexture', 180, 118, 42);
    ui.canvas.drawText('Skia surface sampled by GL', 184, 162, 24);
    ui.flush();

    sys.canvas.clear('#00000000');
    sys.canvas.setFillColor('#F4D35E');
    sys.canvas.drawCircle(input.mouse.x, input.mouse.y, 56);

    sys.gl.bindScreen();
    sys.gl.use(shader);
    sys.gl.setUniform1f(shader, 'u_time_scale', timestamp * 0.001)
    sys.gl.texture(shader, 'u_ui', ui, 1)
    sys.gl.drawFullscreen(shader);

    sys.animation.requestFrame(frame);
}

sys.animation.requestFrame(frame);
