/** @type {(value: number, low: number, high: number) => number} */
export const clamp = (value, low, high) => Math.max(low, Math.min(high, value));

/** @type {(from: number, to: number, fraction: number) => number} */
export const lerp = (from, to, fraction) => from + (to - from) * fraction;

/** Fraction of the remaining distance to cover this frame for an exponential
 *  approach with the given half-life (seconds), independent of frame rate.
 *  @type {(halfLife: number, dt: number) => number} */
export const smoothing = (halfLife, dt) => 1 - Math.pow(0.5, dt / Math.max(0.0001, halfLife));

/** A value that eases toward `target` (kept for compatibility; `Spring` and
 *  `ui.spring` are the richer replacement). */
export class Smoothed {
    /** @param {number} [value] @param {number} [halfLife] */
    constructor(value = 0, halfLife = 0.15) {
        this.value = value;
        this.target = value;
        this.halfLife = halfLife;
    }
    /** @param {number} dt @returns {number} */
    update(dt) {
        this.value = lerp(this.value, this.target, smoothing(this.halfLife, dt));
        return this.value;
    }
}
