/** Content: wrapped paragraphs and shimmering skeleton placeholders. */

import { mixColors } from '../color.js';

/** @typedef {import('../layout.js').Rect} Rect */
/** @typedef {import('../color.js').Color} Color */
/** @typedef {{ size?: number, color?: Color, align?: 'left' | 'center' | 'right',
 *   lineHeight?: number, maxLines?: number }} ParagraphOptions */
/** @typedef {{ width: number, height: number, lines: number }} ParagraphMetrics */
/** @typedef {string | { text: string, size?: number, color?: Color, font?: string }} RichTextSpan */

const LINE_HEIGHT = 1.25;

/** @param {any} ui @param {any} ctx */
export function install(ui, ctx) {
    const t = () => ctx.theme;

    /** @param {ParagraphOptions} options */
    function layout(options) {
        return {
            align: options.align || 'left',
            lineHeight: options.lineHeight || LINE_HEIGHT,
            maxLines: options.maxLines || 0,
        };
    }

    /** Size of `text` wrapped to `width` without drawing it.
     *  @param {string} text @param {number} width @param {ParagraphOptions} [options] @returns {ParagraphMetrics} */
    function measureParagraph(text, width, options = {}) {
        const size = options.size || ctx.sp(t().font);
        if (ctx.canvasHas('measureParagraph'))
            return sys.canvas.measureParagraph(String(text), width, size, layout(options));
        // Older runtimes: a single truncated line.
        return { width: Math.min(width, ctx.measure(String(text), size)), height: size * layout(options).lineHeight, lines: 1 };
    }

    /** Multi-line text wrapped to `rect.width`, starting at the top of
     *  `rect`. Returns its size; the height does not depend on `rect.height`.
     *  @param {string} text @param {Rect} rect @param {ParagraphOptions} [options] @returns {ParagraphMetrics} */
    function paragraph(text, rect, options = {}) {
        const size = options.size || ctx.sp(t().font);
        ctx.describeText(rect, String(text));
        sys.canvas.setFillColor(ctx.paint(options.color || t().ink));
        if (ctx.canvasHas('drawParagraph'))
            return sys.canvas.drawParagraph(String(text), rect.x, rect.y, rect.width, size, layout(options));
        const line = ctx.truncate(String(text), rect.width, size);
        const height = size * layout(options).lineHeight;
        sys.canvas.drawText(line, rect.x, rect.y + height / 2 + size * 0.35, size);
        return { width: ctx.measure(line, size), height, lines: 1 };
    }

    /** Spans as plain text, for older runtimes and screen readers. @param {RichTextSpan[]} spans */
    const plain = spans => spans.map(span => (typeof span === 'string' ? span : span.text)).join('');

    /** Size of styled spans wrapped to `width` without drawing them.
     *  @param {RichTextSpan[]} spans @param {number} width @param {ParagraphOptions} [options] @returns {ParagraphMetrics} */
    function measureRichText(spans, width, options = {}) {
        const size = options.size || ctx.sp(t().font);
        if (ctx.canvasHas('measureRichText'))
            return sys.canvas.measureRichText(spans, width, { ...layout(options), size });
        return measureParagraph(plain(spans), width, options);
    }

    /** Paragraph of styled spans (strings or { text, size?, color?, font? }),
     *  wrapped to `rect.width` from the top of `rect`. Span colors are used as
     *  given; text without one uses `options.color`.
     *  @param {RichTextSpan[]} spans @param {Rect} rect @param {ParagraphOptions} [options] @returns {ParagraphMetrics} */
    function richText(spans, rect, options = {}) {
        if (!ctx.canvasHas('drawRichText')) return paragraph(plain(spans), rect, options);
        const size = options.size || ctx.sp(t().font);
        ctx.describeText(rect, plain(spans));
        sys.canvas.setFillColor(ctx.paint(options.color || t().ink));
        return sys.canvas.drawRichText(spans, rect.x, rect.y, rect.width, { ...layout(options), size });
    }

    /** A loading placeholder with a light band sweeping across it.
     *  @param {Rect} rect @param {{ radius?: number }} [options] */
    function skeleton(rect, options = {}) {
        const theme = t();
        const radius = options.radius === undefined ? ctx.px(theme.radius) : options.radius;
        const base = ctx.paint(theme.raised);
        if (ctx.reducedMotion || !ctx.canvasHas('setGradient')) {
            ctx.fill(rect, theme.raised, radius);
            return;
        }
        ctx.animating = true;
        const shine = ctx.paint(mixColors(theme.raised, theme.surface, 0.7));
        // The band crosses the screen every 1.4 s, aligned across skeletons.
        const span = Math.max(ctx.bounds.width, 1);
        const band = Math.max(ctx.px(160), span * 0.25);
        const start = ctx.bounds.x - band + ((ctx.input.totalTime || 0) / 1.4 % 1) * (span + band * 2);
        sys.canvas.setFillColor(base);
        sys.canvas.setGradient('linear', start, 0, start + band, 0, [base, shine, base]);
        sys.canvas.drawRoundRect(rect.x, rect.y, rect.width, rect.height, radius, radius);
        sys.canvas.setGradient(null);
    }

    Object.assign(ui, { paragraph, measureParagraph, richText, measureRichText, skeleton });
}
