/** Progress: determinate and indeterminate bars, and spinners. */

import { clamp } from '../math.js';
import { springs } from '../motion.js';

/** @typedef {import('../layout.js').Rect} Rect */

/** @param {any} ui @param {any} ctx */
export function install(ui, ctx) {
    const t = () => ctx.theme;

    /** A progress bar. `value` in 0..1 glides to new values; null shows an
     *  indeterminate band sweeping across.
     *  @param {string} id @param {Rect} rect @param {number | null} value @param {{ label?: string }} [options] */
    function progress(id, rect, value, options = {}) {
        const theme = t(), radius = rect.height / 2;
        const key = ctx.fullId(id);
        ctx.describe(key, rect, value === null
            ? { role: 'slider', label: options.label || 'Progress', value: 'busy' }
            : { role: 'slider', label: options.label || 'Progress', value: Math.round(value * 100) + '%', min: 0, max: 1, rangeValue: value });
        ctx.fill(rect, theme.raised, radius);
        if (value === null) {
            if (ctx.reducedMotion) {
                ctx.fillAccent(key, rect, theme.accent, radius);
                return;
            }
            ctx.animating = true;
            // A band of a third of the width crosses every 1.2 s, easing in and out.
            const phase = ((ctx.input.totalTime || 0) / 1.2) % 1;
            const eased = phase < 0.5 ? 2 * phase * phase : 1 - 2 * (1 - phase) * (1 - phase);
            const band = rect.width / 3;
            const x = rect.x - band + eased * (rect.width + band);
            const clip = ctx.pushClip(rect, radius);
            ctx.fillAccent(key, { x, y: rect.y, width: band, height: rect.height }, theme.accent, radius);
            ctx.popClip(clip);
            return;
        }
        const shown = clamp(ctx.springAt(key + '#value', clamp(value, 0, 1), springs.gentle), 0, 1);
        if (shown > 0.001) ctx.fillAccent(key, { ...rect, width: Math.max(rect.height, rect.width * shown) }, theme.accent, radius);
    }

    /** A spinning arc, for work of unknown length. @param {Rect} rect @param {{ label?: string }} [options] */
    function spinner(rect, options = {}) {
        const theme = t(), px = ctx.px;
        ctx.describeText(rect, options.label || 'Loading', 'image');
        const size = Math.min(rect.width, rect.height);
        const box = { x: rect.x + (rect.width - size) / 2 + px(2), y: rect.y + (rect.height - size) / 2 + px(2),
            width: size - px(4), height: size - px(4) };
        const time = ctx.reducedMotion ? 0 : ctx.input.totalTime || 0;
        if (!ctx.reducedMotion) ctx.animating = true;
        sys.canvas.setStrokeColor(ctx.paint(theme.border));
        sys.canvas.setStrokeWidth(px(3));
        sys.canvas.setStrokeCap?.('round');
        sys.canvas.drawArc?.(box.x, box.y, box.width, box.height, 0, 360, false);
        // The arc turns while its length breathes.
        const sweep = 60 + 200 * (0.5 + 0.5 * Math.sin(time * 3));
        sys.canvas.setStrokeColor(ctx.paint(theme.accent));
        sys.canvas.drawArc?.(box.x, box.y, box.width, box.height, (time * 360) % 360, sweep, false);
        sys.canvas.setStrokeCap?.('butt');
    }

    Object.assign(ui, { progress, spinner });
}
