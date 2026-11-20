// Desktop: budo run tests/android_fullscreen_immediate
// Android: budo android-apk tests/android_fullscreen_immediate --install
// Web (with the Web runtime rebuilt): budo web-serve tests/android_fullscreen_immediate
// Expect solid red at launch and after the first tap or click (target resize).
// After the second tap or click, expect solid green (default canvas presentation).
// After the third, expect a blue top half and red bottom half to stay visible.
// Wait a moment, then tap or click again: expect green if the idle timer ran.
const background = sys.gl.createProgram('fullscreen.vert', 'background.frag');
const composite = sys.gl.createProgram('fullscreen.vert', 'composite.frag');
let width = 32;
let height = 24;
const target = sys.gl.createRenderTarget(width, height);
let phase = 0;
let blue = 0;
let idleTimerRan = false;

function frame() {
    if (sys.input.get().pointer.pressed) {
        if (phase === 0) {
            width = 53;
            height = 37;
            sys.gl.resizeRenderTarget(target, width, height);
        }
        phase = Math.min(phase + 1, 4);
        if (phase === 3) sys.timer.once(100, () => { idleTimerRan = true; });
    }

    // Alternate colors so a stale Skia flush cannot pass the canvas check.
    blue = phase === 2 || phase === 4 ? 0 : 1 - blue;
    sys.canvas.clear(phase === 4 && !idleTimerRan ? '#ff00ff' : blue ? '#00ffff' : '#00ff00');
    sys.gl.bindRenderTarget(target);
    sys.gl.setUniform2f(background, 'u_expectedResolution', width, height);
    sys.gl.setUniform1f(background, 'u_expectedBlue', blue);
    sys.gl.drawFullscreenImmediate(background);

    if (phase < 2 || phase === 3) {
        sys.gl.bindScreen();
        sys.gl.setUniform1i(composite, 'u_idlePattern', phase === 3 ? 1 : 0);
        sys.gl.bindTexture(composite, 'u_background', target, 1);
        sys.gl.drawFullscreen(composite);
    }
    if (phase === 3) sys.animation.waitForInput(frame);
    else sys.animation.requestFrame(frame);
}
sys.animation.requestFrame(frame);
