// Tempo estimation from note onsets.
//
// Every candidate tempo from MIN_BPM to MAX_BPM gets a score: how well the
// intervals between recent onsets fall on its beat grid. An interval of a
// whole number of beats scores fully; one landing on a half, a third, or a
// quarter of a beat scores less, so subdivisions (eighths, triplets,
// sixteenths) support their beat instead of pulling the tempo up. Limiting
// the range to 50-150 BPM leaves out the multiples (200 for eighths at 100,
// or 50 for quarters at 100 also fits half the intervals but less well).

export const MIN_BPM = 50;
export const MAX_BPM = 150;

// Onsets closer than this are one onset (a chord, or a flam).
const CHORD_SECONDS = 0.04;
// Timing a player may be off by, in seconds (the grid's tolerance).
const JITTER_SECONDS = 0.022;
// Older onsets matter less: their weight halves every HALF_LIFE seconds.
const HALF_LIFE = 5;
const WINDOW_SECONDS = 10;
const MAX_ONSETS = 32;
// Intervals considered: up to two beats at the slowest tempo, and a bar at
// the fastest.
const MAX_INTERVAL = 2.5;
// Credit for an interval on a whole beat, a half, a quarter, a third.
const CREDIT_BEAT = 1, CREDIT_HALF = 0.6, CREDIT_QUARTER = 0.35, CREDIT_THIRD = 0.4;
// Tempos that fit (almost) as well as the best, listed as alternatives:
// without accents, even eighths at 75 BPM are even quarters at 150.
const RELATED = [0.5, 2, 2 / 3, 1.5, 1 / 3, 3, 0.75, 4 / 3];
const ALTERNATIVE_RATIO = 0.85;

export class TempoEstimator {
    constructor() {
        /** @type {{time: number, weight: number}[]} seconds, and loudness */
        this.onsets = [];
        this.bpm = 0;          // 0 until there is enough to go on
        this.confidence = 0;   // 0 to 1
        this.phase = 0;        // seconds: a beat falls at phase + k * 60 / bpm
        this.scores = null;    // per whole BPM, for display
        this.alternatives = []; // other tempos that fit almost as well
    }

    clear() {
        this.onsets = [];
        this.bpm = 0;
        this.confidence = 0;
    }

    // A note started at `time` (seconds, any monotonic clock), loudness 0..1.
    addOnset(time, weight = 1) {
        const last = this.onsets[this.onsets.length - 1];
        if (last && time - last.time < CHORD_SECONDS) {
            last.weight = Math.min(2, last.weight + weight * 0.5); // a chord: a stronger onset
            last.velocity = Math.max(last.velocity, weight);
            return false;
        }
        // Loud notes (accents, downbeats) weigh much more than soft ones.
        this.onsets.push({ time, velocity: weight, weight: 0.2 + 0.8 * weight * weight });
        while (this.onsets.length > MAX_ONSETS || (this.onsets.length && time - this.onsets[0].time > WINDOW_SECONDS))
            this.onsets.shift();
        return true;
    }

    // The pairs of onsets and their weights, newest weighing most.
    pairs(now) {
        const list = [];
        const o = this.onsets;
        for (let j = o.length - 1; j > 0; j--) {
            const recency = Math.pow(0.5, (now - o[j].time) / HALF_LIFE);
            for (let i = j - 1; i >= 0; i--) {
                const interval = o[j].time - o[i].time;
                if (interval > MAX_INTERVAL) break;
                if (interval < 0.08) continue;
                // Short intervals say more about the beat than long ones.
                const weight = recency * o[i].weight * o[j].weight / (1 + interval);
                list.push(interval, weight);
            }
        }
        return list;
    }

    // How well the intervals fit a beat of `period` seconds, 0 to 1. Each
    // interval is placed on the quarter-beat grid (whole, half, and quarter
    // beats) and on the third-beat grid; a bump kernel (no exp, this runs in
    // an interpreter) scores its distance to the nearest point.
    score(pairs, period) {
        let total = 0, fit = 0;
        const quarter = period / 4, third = period / 3;
        const reach = 2.5 * JITTER_SECONDS, inverse = 1 / (reach * reach);
        for (let p = 0; p < pairs.length; p += 2) {
            const interval = pairs[p], weight = pairs[p + 1];
            const m = Math.round(interval / quarter);
            let e = interval - m * quarter;
            let k = 1 - e * e * inverse;
            let best = k > 0 ? k * k * ((m & 3) === 0 ? CREDIT_BEAT : (m & 1) === 0 ? CREDIT_HALF : CREDIT_QUARTER) : 0;
            const n = Math.round(interval / third);
            if (n % 3 !== 0) {
                e = interval - n * third;
                k = 1 - e * e * inverse;
                if (k > 0 && k * k * CREDIT_THIRD > best) best = k * k * CREDIT_THIRD;
            }
            fit += weight * best;
            total += weight;
        }
        return total > 0 ? fit / total : 0;
    }

    // How consistently a tempo's beats carry the accents: the onsets on its
    // beats against the others. `diff` is how much louder the on-beat ones
    // are; `fraction` how many of them are louder than the others' average.
    // Null when there are too few onsets on or off its beats to tell.
    accentAlignment(bpm, now) {
        const period = 60 / bpm;
        const phase = this.findPhase(period, now, true);
        let onSum = 0, offSum = 0;
        const on = [];
        let offCount = 0;
        for (const o of this.onsets) {
            let position = ((o.time - phase) / period) % 1;
            if (position < 0) position += 1;
            if (Math.min(position, 1 - position) < 0.1) {
                on.push(o.velocity);
                onSum += o.velocity;
            } else {
                offSum += o.velocity;
                offCount++;
            }
        }
        if (on.length < 3 || offCount < 3) return null;
        const offMean = offSum / offCount;
        return {
            diff: onSum / on.length - offMean,
            fraction: on.filter((v) => v > offMean + 0.03).length / on.length,
        };
    }

    // Intervals alone cannot tell even eighths at 75 BPM from quarters at 150,
    // or even sixteenths at 90 from triplets at 120: accents can. Among the
    // related tempos that fit almost as well, one whose beats consistently
    // carry the accents (most of its on-beat onsets louder than the rest) is
    // the beat. An accented downbeat alone (quarters, one loud beat in four)
    // is not consistent: half of the slower tempo's beats are not louder.
    accentedTempo(pairs, bpm, bestScore, now) {
        const current = this.accentAlignment(bpm, now);
        let choice = 0, choiceDiff = current ? current.diff + 0.05 : 0.08;
        for (const ratio of RELATED) {
            const related = bpm * ratio;
            if (related < MIN_BPM || related > MAX_BPM || this.score(pairs, 60 / related) < 0.7 * bestScore) continue;
            const a = this.accentAlignment(related, now);
            if (a && a.fraction >= 0.8 && a.diff > choiceDiff) {
                choice = related;
                choiceDiff = a.diff;
            }
        }
        return choice;
    }

    // Re-estimate the tempo. `now` is the current time on the onsets' clock.
    estimate(now) {
        if (this.onsets.length < 4) {
            this.bpm = 0;
            this.confidence = 0;
            return this;
        }
        const pairs = this.pairs(now);
        if (pairs.length < 6) return this;
        // Coarse: every whole BPM.
        const scores = new Float32Array(MAX_BPM - MIN_BPM + 1);
        let bestBpm = MIN_BPM, bestScore = -1;
        for (let bpm = MIN_BPM; bpm <= MAX_BPM; bpm++) {
            const s = this.score(pairs, 60 / bpm);
            scores[bpm - MIN_BPM] = s;
            if (s > bestScore) {
                bestScore = s;
                bestBpm = bpm;
            }
        }
        // Fine: a twentieth of a BPM around the best.
        let fineBpm = bestBpm;
        for (let bpm = bestBpm - 1; bpm <= bestBpm + 1; bpm += 0.05) {
            if (bpm < MIN_BPM || bpm > MAX_BPM) continue;
            const s = this.score(pairs, 60 / bpm);
            if (s > bestScore) {
                bestScore = s;
                fineBpm = bpm;
            }
        }
        // Accents may say the beat is another, related tempo.
        const accented = this.accentedTempo(pairs, fineBpm, bestScore, now);
        if (accented) {
            fineBpm = accented;
            bestScore = this.score(pairs, 60 / accented);
        }
        this.scores = scores;
        this.bpm = fineBpm;
        // Confidence: the fit itself, less how close another tempo (not a
        // neighbour of this one) comes to it.
        // Related tempos (half, double, ...) are not rivals: they are listed
        // as alternatives instead.
        const related = (bpm) => Math.abs(bpm - fineBpm) <= 3 ||
            RELATED.some((ratio) => Math.abs(bpm - fineBpm * ratio) <= 3);
        let rival = 0;
        for (let bpm = MIN_BPM; bpm <= MAX_BPM; bpm++)
            if (!related(bpm)) rival = Math.max(rival, scores[bpm - MIN_BPM]);
        this.confidence = Math.max(0, Math.min(1, bestScore * (1 - 0.8 * rival / Math.max(bestScore, 1e-6)) * 2));
        this.phase = this.findPhase(60 / fineBpm, now);
        this.alternatives = [];
        for (const ratio of RELATED) {
            const bpm = fineBpm * ratio;
            if (bpm < MIN_BPM || bpm > MAX_BPM) continue;
            if (this.score(pairs, 60 / bpm) >= ALTERNATIVE_RATIO * bestScore) this.alternatives.push(bpm);
        }
        return this;
    }

    // Where the beats fall: the circular mean of the onsets' positions in the
    // beat, recent and loud ones weighing most.
    findPhase(period, now, accentsOnly = false) {
        let x = 0, y = 0;
        let mean = 0;
        if (accentsOnly) for (const o of this.onsets) mean += o.velocity / this.onsets.length;
        for (const o of this.onsets) {
            // With accentsOnly, the louder-than-average onsets set the phase.
            const loudness = accentsOnly ? Math.max(0, o.velocity - mean) : o.weight;
            const w = loudness * Math.pow(0.5, (now - o.time) / HALF_LIFE);
            const angle = 2 * Math.PI * (o.time / period);
            x += w * Math.cos(angle);
            y += w * Math.sin(angle);
        }
        let phase = Math.atan2(y, x) / (2 * Math.PI) * period;
        if (phase < 0) phase += period;
        return phase;
    }
}
