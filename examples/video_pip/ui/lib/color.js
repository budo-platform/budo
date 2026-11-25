/** Color parsing and perceptual interpolation. Budo colors are "#RGB",
 *  "#RRGGBB", "#RRGGBBAA" strings, or numeric 0xAARRGGBB. */

/** @typedef {string | number} Color */
/** @typedef {[number, number, number, number]} RGBA channels 0..1 */

/** @type {Map<Color, RGBA>} */
const parsed = new Map();

/** @param {Color} color @returns {RGBA} */
export function parseColor(color) {
    const cached = parsed.get(color);
    if (cached) return cached;
    /** @type {RGBA} */
    let rgba = [0, 0, 0, 1];
    if (typeof color === 'number') {
        rgba = [((color >>> 16) & 255) / 255, ((color >>> 8) & 255) / 255,
            (color & 255) / 255, ((color >>> 24) & 255) / 255];
    } else if (typeof color === 'string' && color[0] === '#') {
        let hex = color.slice(1);
        if (hex.length === 3 || hex.length === 4) hex = hex.split('').map(c => c + c).join('');
        const n = parseInt(hex.slice(0, 6), 16);
        rgba = [((n >> 16) & 255) / 255, ((n >> 8) & 255) / 255, (n & 255) / 255,
            hex.length >= 8 ? parseInt(hex.slice(6, 8), 16) / 255 : 1];
    }
    if (parsed.size > 512) parsed.clear();
    parsed.set(color, rgba);
    return rgba;
}

/** @param {number} value @returns {string} */
const hex2 = value => Math.round(Math.max(0, Math.min(1, value)) * 255).toString(16).padStart(2, '0');

/** @param {RGBA} rgba @returns {string} */
export function formatColor([r, g, b, a]) {
    return '#' + hex2(r) + hex2(g) + hex2(b) + (a >= 0.999 ? '' : hex2(a));
}

/** @param {Color} color @param {number} alpha @returns {Color} */
export function withAlpha(color, alpha) {
    const [r, g, b, a] = parseColor(color);
    return formatColor([r, g, b, a * alpha]);
}

const toLinear = c => (c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4));
const toSrgb = c => (c <= 0.0031308 ? c * 12.92 : 1.055 * Math.pow(c, 1 / 2.4) - 0.055);

/** @param {RGBA} rgba @returns {[number, number, number]} OKLab */
function toOklab([r, g, b]) {
    const lr = toLinear(r), lg = toLinear(g), lb = toLinear(b);
    const l = Math.cbrt(0.4122214708 * lr + 0.5363325363 * lg + 0.0514459929 * lb);
    const m = Math.cbrt(0.2119034982 * lr + 0.6806995451 * lg + 0.1073969566 * lb);
    const s = Math.cbrt(0.0883024619 * lr + 0.2817188376 * lg + 0.6299787005 * lb);
    return [0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
        1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
        0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s];
}

/** @param {[number, number, number]} lab @returns {[number, number, number]} sRGB */
function fromOklab([L, a, b]) {
    const l = (L + 0.3963377774 * a + 0.2158037573 * b) ** 3;
    const m = (L - 0.1055613458 * a - 0.0638541728 * b) ** 3;
    const s = (L - 0.0894841775 * a - 1.2914855480 * b) ** 3;
    return [toSrgb(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s),
        toSrgb(-1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s),
        toSrgb(-0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s)];
}

/** Interpolate in OKLab, so a transition stays perceptually even (no muddy
 *  midpoints). Alpha interpolates linearly; a fully transparent end takes the
 *  other end's hue, so fading in or out does not shift the color.
 *  @param {Color} from @param {Color} to @param {number} t @returns {Color} */
export function mixColors(from, to, t) {
    if (t <= 0) return from;
    if (t >= 1) return to;
    const a = parseColor(from), b = parseColor(to);
    const startRgb = a[3] === 0 ? b : a, endRgb = b[3] === 0 ? a : b;
    const p = toOklab(startRgb), q = toOklab(endRgb);
    const [r, g, bl] = fromOklab([p[0] + (q[0] - p[0]) * t, p[1] + (q[1] - p[1]) * t, p[2] + (q[2] - p[2]) * t]);
    return formatColor([r, g, bl, a[3] + (b[3] - a[3]) * t]);
}
