import { createUI, themes } from './ui/budo-ui.js';
import { createPlayer } from './player.js';

const BACKGROUND_PATH = 'assets/background.mp4';
const INSET_PATH = 'assets/inset.mp4';
const OUTPUT_PATH = 'files/picture-in-picture.mp4';

const ui = createUI();
ui.setTheme(themes.dark);
const player = createPlayer(sys.video, BACKGROUND_PATH, INSET_PATH, OUTPUT_PATH);
const state = player.state;
let available = false;
let shader = -1;
let seekDraft = null;

if (!sys.video) {
    state.previewStatus = 'Video support unavailable. Rebuild with ENABLE_VIDEO=ON.';
} else if (!sys.files.exists(BACKGROUND_PATH) || !sys.files.exists(INSET_PATH)) {
    state.previewStatus = 'Missing fixtures. Run sh examples/video_pip/generate-fixtures.sh from the repository root.';
} else {
    available = true;
    player.start();
}

function time(us) {
    const seconds = Math.max(0, Math.floor(us / 1000000));
    return `${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, '0')}`;
}

function drawVideo(rect) {
    if (!state.texture || rect.width <= 0 || rect.height <= 0) return;
    try {
        if (shader === -1) {
            shader = sys.gl.createProgram('video_pip.vert', 'video_pip.frag');
            if (shader <= 0) throw new Error(sys.gl.getLastError() || 'Could not create video shader');
        }
        // Flush the canvas first; the borrowed texture occupies only the video rectangle.
        sys.gl.bindScreen();
        sys.gl.useProgram(shader);
        sys.gl.texture(shader, 'u_video', state.texture, 1);
        sys.gl.drawRegionImmediate(shader, rect.x, rect.y, rect.width, rect.height, -1);
    } catch (error) {
        shader = -1;
        player.close(`Video rendering error: ${error.message || error}`);
    }
}

function frame() {
    const input = sys.input.get();
    ui.begin(input);
    ui.clear();
    const dp = ui.dp;
    const page = ui.inset(ui.bounds, dp(ui.bounds.width < dp(500) ? 10 : 20));
    const compact = page.width < dp(560);
    const scale = Math.min(1, Math.max(0.35, page.height / dp(compact ? 410 : 360)));
    const unit = n => dp(n * scale);
    const gap = unit(6);
    const controlHeight = unit(40);
    const [title, videoArea, seekLabel, seekBar, buttons, previewStatus, exportStatus, notice] = ui.rows(page,
        [unit(28), { weight: 1 }, unit(20), unit(28), compact ? controlHeight * 2 + gap : controlHeight,
            unit(35), unit(35), unit(36)], gap);

    ui.label('PICTURE-IN-PICTURE', title, { size: unit(20) });
    const videoWidth = Math.max(0, Math.min(videoArea.width, videoArea.height * 16 / 9, dp(800)));
    const videoHeight = videoWidth * 9 / 16;
    const videoRect = { x: videoArea.x + (videoArea.width - videoWidth) / 2,
        y: videoArea.y + (videoArea.height - videoHeight) / 2, width: videoWidth, height: videoHeight };
    ui.fill(videoRect, '#090D13');

    const ready = !!state.preview && !!state.preview.id && state.durationUs > 0;
    const end = player.lastPosition();
    if (!ready) seekDraft = null;
    ui.label(`Position  ${time(seekDraft === null ? state.positionUs : seekDraft)} / ${time(state.durationUs)}` +
        (seekDraft === null ? '' : '  (release to seek)'), seekLabel,
        { color: ui.colors.muted, size: unit(13) });
    if (ready && end > 0) {
        const shown = seekDraft === null ? state.positionUs : seekDraft;
        const picked = Math.round(ui.slider('seek', seekBar, shown, { min: 0, max: end, label: 'Preview position' }));
        if (picked !== shown) {
            if (input.pointer.down || seekDraft !== null) seekDraft = picked;
            else player.seek(picked);
        }
        if (!input.pointer.down && seekDraft !== null) {
            player.seek(seekDraft);
            seekDraft = null;
        }
    } else {
        ui.fill({ x: seekBar.x + unit(10), y: seekBar.y + seekBar.height / 2 - unit(2),
            width: Math.max(0, seekBar.width - unit(20)), height: unit(4) }, ui.colors.border, unit(2));
    }

    const rows = compact ? ui.rows(buttons, [controlHeight, controlHeight], gap) : [buttons];
    const first = ui.columns(rows[0], compact ? [{ weight: 1 }, { weight: 1 }] :
        [{ weight: 1 }, { weight: 1 }, { weight: 1 }, { weight: 1 }], gap);
    const second = compact ? ui.columns(rows[1], [{ weight: 1 }, { weight: 1 }], gap) : first.slice(2);
    if (ui.button('play', state.paused || state.ended ? 'Play' : 'Pause', first[0], { primary: true, enabled: ready })) player.toggle();
    if (ui.button('restart', state.preview ? 'Restart' : 'Open', first[1], { enabled: available && (!state.preview || ready) })) player.restart();
    if (ui.button('close', 'Close', second[0], { enabled: !!state.preview && !!state.preview.id })) player.close();
    // Export and cancel share a slot; terminal callbacks re-enable export.
    if (state.export) {
        if (ui.button('cancel', state.export.cancelling ? 'Cancelling' : 'Cancel', second[1],
            { enabled: !!state.export.id && !state.export.cancelling })) player.cancelExport();
    } else if (ui.button('export', 'Export', second[1], { enabled: available })) player.startExport();

    ui.paragraph(state.previewStatus, previewStatus, { size: unit(13), color: ui.colors.ink, maxLines: 2 });
    ui.paragraph(state.exportStatus, exportStatus, { size: unit(13), color: ui.colors.muted, maxLines: 2 });
    ui.paragraph('Silent preview and export: audio, subtitles and other tracks are omitted.', notice,
        { size: unit(12), color: ui.colors.muted, maxLines: 2 });
    ui.end();
    drawVideo(videoRect);
    sys.animation.requestFrame(frame);
}

sys.animation.requestFrame(frame);
