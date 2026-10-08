/** Spring physics for UI motion. */

/** @typedef {{ stiffness: number, damping: number }} SpringPreset */

/** Mass is 1. Critical damping is 2 * sqrt(stiffness): above it a spring
 *  settles without overshoot, below it bounces. */
export const springs = {
    /** @type {SpringPreset} quick, no visible overshoot */
    default: { stiffness: 520, damping: 46 },
    /** @type {SpringPreset} fast feedback (press, toggles) */
    snappy: { stiffness: 900, damping: 60 },
    /** @type {SpringPreset} slow and soft (large movements, fades) */
    gentle: { stiffness: 170, damping: 26 },
    /** @type {SpringPreset} playful overshoot */
    bouncy: { stiffness: 420, damping: 18 },
};

export class Spring {
    /** @param {number} [value] @param {SpringPreset} [preset] @param {number} [precision]
     *  distance and speed under which the spring snaps to its target */
    constructor(value = 0, preset = springs.default, precision = 0.001) {
        this.value = value;
        this.target = value;
        this.velocity = 0;
        this.stiffness = preset.stiffness;
        this.damping = preset.damping;
        this.precision = precision;
    }

    /** @param {SpringPreset} preset */
    setPreset(preset) {
        this.stiffness = preset.stiffness;
        this.damping = preset.damping;
    }

    get settled() {
        return this.value === this.target && this.velocity === 0;
    }

    /** Jump to `value` with no motion. @param {number} value */
    snap(value = this.target) {
        this.value = this.target = value;
        this.velocity = 0;
    }

    /** Advance by dt seconds (frame-rate independent: fixed small substeps).
     *  Changing `target` mid-flight keeps the current velocity, so motion
     *  redirects smoothly instead of restarting.
     *  @param {number} dt @returns {number} */
    step(dt) {
        if (this.settled) return this.value;
        let remaining = Math.min(Math.max(dt, 0), 0.1);
        while (remaining > 0) {
            const h = Math.min(remaining, 1 / 240);
            const accel = -this.stiffness * (this.value - this.target) - this.damping * this.velocity;
            this.velocity += accel * h;
            this.value += this.velocity * h;
            remaining -= h;
        }
        if (Math.abs(this.value - this.target) < this.precision &&
            Math.abs(this.velocity) < this.precision * 10)
            this.snap();
        return this.value;
    }
}
