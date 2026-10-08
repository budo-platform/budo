// The 3D scene is drawn into an offscreen target at a render scale (dynamic
// resolution: a slow GPU draws fewer pixels and the result is upscaled), then
//   bright pass -> blur H -> blur V   (bloom, at a fraction of the resolution)
//   composite: heat shimmer, bloom, color grade, vignette, speed lines, HUD.
// The HUD keeps the full resolution. With effects off, only the composite runs.
// begin() binds the scene target; end() draws the result to the screen.

// Mesh draws store a target bottom-up; passes read and write top-down.
const SCENE_FLIP = 1.0;
const NO_LIFT = new Float32Array([0, 0, 0]);
const NO_GAIN = new Float32Array([1, 1, 1]);

// downscale: bloom resolution divisor; blurRounds: 1 is cheap, 2 is wider.
export function createPost(downscale, blurRounds) {
    const bright = sys.gl.createProgram('post.vert', 'bloom_bright.frag');
    const blur = sys.gl.createProgram('post.vert', 'bloom_blur.frag');
    const composite = sys.gl.createProgram('post.vert', 'composite.frag');
    let width = 0, height = 0, bloomWidth = 0, bloomHeight = 0;
    let scene = 0, bloomA = 0, bloomB = 0;

    function ensureTargets(renderScale) {
        const w = Math.max(1, Math.round(sys.window.getWidth() * renderScale));
        const h = Math.max(1, Math.round(sys.window.getHeight() * renderScale));
        if (w === width && h === height) return;
        width = w;
        height = h;
        bloomWidth = Math.max(1, Math.ceil(w / downscale));
        bloomHeight = Math.max(1, Math.ceil(h / downscale));
        if (!scene) {
            scene = sys.gl.createRenderTarget(width, height, true);
            bloomA = sys.gl.createRenderTarget(bloomWidth, bloomHeight);
            bloomB = sys.gl.createRenderTarget(bloomWidth, bloomHeight);
        } else {
            sys.gl.resizeRenderTarget(scene, width, height);
            sys.gl.resizeRenderTarget(bloomA, bloomWidth, bloomHeight);
            sys.gl.resizeRenderTarget(bloomB, bloomWidth, bloomHeight);
        }
    }

    // renderScale: the scene's resolution relative to the screen, 0..1.
    function begin(renderScale) {
        ensureTargets(renderScale);
        sys.gl.bindRenderTarget(scene);
    }

    // look: { effects, threshold, bloom, horizon, haze, lift, gain,
    //         saturation, contrast, speedFx, vignette }.
    function end(look) {
        const effects = look.effects;
        if (effects) {
            sys.gl.setUniform2f(bright, 'u_texel', 1 / width, 1 / height);
            sys.gl.setUniform1f(bright, 'u_threshold', look.threshold);
            sys.gl.setUniform1f(bright, 'u_scene_flip', SCENE_FLIP);
            sys.gl.bindTexture(bright, 'u_scene', scene, 1);
            sys.gl.drawRegion(bright, 0, 0, bloomWidth, bloomHeight, bloomA);

            // Each round is a horizontal then a vertical blur, wider each time.
            for (let round = 0; round < blurRounds; round++) {
                const spread = 1.4 + round * 1.8;
                sys.gl.setUniform2f(blur, 'u_step', spread / bloomWidth, 0);
                sys.gl.bindTexture(blur, 'u_source', bloomA, 1);
                sys.gl.drawRegion(blur, 0, 0, bloomWidth, bloomHeight, bloomB);
                sys.gl.setUniform2f(blur, 'u_step', 0, spread / bloomHeight);
                sys.gl.bindTexture(blur, 'u_source', bloomB, 1);
                sys.gl.drawRegion(blur, 0, 0, bloomWidth, bloomHeight, bloomA);
            }
        }

        sys.gl.bindScreen();
        sys.gl.bindTexture(composite, 'u_scene', scene, 1);
        sys.gl.bindTexture(composite, 'u_bloom', bloomA, 2);
        sys.gl.setUniform1f(composite, 'u_scene_flip', SCENE_FLIP);
        sys.gl.setUniform1f(composite, 'u_bloom_strength', effects ? look.bloom : 0);
        sys.gl.setUniform1f(composite, 'u_horizon', look.horizon);
        sys.gl.setUniform1f(composite, 'u_haze', effects ? look.haze : 0);
        sys.gl.setUniform3fv(composite, 'u_lift', effects ? look.lift : NO_LIFT);
        sys.gl.setUniform3fv(composite, 'u_gain', effects ? look.gain : NO_GAIN);
        sys.gl.setUniform1f(composite, 'u_saturation', effects ? look.saturation : 1);
        sys.gl.setUniform1f(composite, 'u_contrast', effects ? look.contrast : 0);
        sys.gl.setUniform1f(composite, 'u_speed_fx', look.speedFx);
        sys.gl.setUniform1f(composite, 'u_vignette', look.vignette);
        sys.gl.drawFullscreenImmediate(composite);
    }

    return { begin, end };
}
