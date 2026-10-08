// Spectrum analysis of the microphone, on the last `size` samples, with
// Budo's native analysis (sys.audio.getSpectrum and sys.audio.detectPitch):
// - the windowed spectrum: the levels of log-spaced frequency rows (for the
//   spectrogram) and the dominant (loudest) frequency;
// - the pitch, by the McLeod pitch method, which finds the fundamental of a
//   voice or an instrument even when a harmonic is louder.

export const SIZES = [128, 256, 512, 1024, 2048, 4096];
export const ROWS = 128;
export const MIN_FREQUENCY = 50;
export const MIN_DB = -90;
const PITCH_OPTIONS = { minFrequency: 50, maxFrequency: 3000, minLevel: -50 };

const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];

export function noteOf(frequency) {
    const midi = 69 + 12 * Math.log2(frequency / 440);
    const nearest = Math.round(midi);
    return {
        name: NOTE_NAMES[((nearest % 12) + 12) % 12] + (Math.floor(nearest / 12) - 1),
        cents: Math.round((midi - nearest) * 100),
    };
}

export class SpectrumAnalyzer {
    constructor(sampleRate, size) {
        this.sampleRate = sampleRate;
        this.size = size;
        this.history = new Float32Array(size); // the last `size` samples, a ring
        this.at = 0;
        this.samples = new Float32Array(size); // the same, oldest first
        this.db = new Float32Array(size / 2); // per bin, up to the Nyquist frequency
        this.maxFrequency = Math.min(12000, sampleRate / 2);
        // Row r covers [edges[r], edges[r + 1]) in FFT bins, log-spaced.
        this.edges = new Float32Array(ROWS + 1);
        const binHz = sampleRate / size;
        for (let r = 0; r <= ROWS; r++) {
            const f = MIN_FREQUENCY * Math.pow(this.maxFrequency / MIN_FREQUENCY, r / ROWS);
            this.edges[r] = f / binHz;
        }
        this.rows = new Uint8Array(ROWS); // the last spectrum, 0..255 per row
        this.dominant = 0; // Hz, 0 when quiet
        // The longest period detectPitch sees is half the samples.
        this.lowestPitch = Math.max(MIN_FREQUENCY, sampleRate / (size / 2));
        this.recent = [0, 0, 0]; // the last estimates, for a median
        this.pitch = 0; // Hz, 0 when there is none
        this.clarity = 0;
    }

    // Row (0..ROWS) of a frequency, for axis labels.
    rowOf(frequency) {
        return ROWS * Math.log(frequency / MIN_FREQUENCY) / Math.log(this.maxFrequency / MIN_FREQUENCY);
    }

    write(samples) {
        const n = this.size, history = this.history;
        let at = this.at;
        for (let i = 0; i < samples.length; i++) {
            history[at] = samples[i];
            at = at + 1 === n ? 0 : at + 1;
        }
        this.at = at;
    }

    analyze() {
        // The ring, oldest first.
        const n = this.size, at = this.at;
        this.samples.set(this.history.subarray(at), 0);
        this.samples.set(this.history.subarray(0, at), n - at);

        sys.audio.getSpectrum(this.samples, this.db);
        this.spectrumRows();
        this.findDominant();

        const result = sys.audio.detectPitch(this.samples, this.sampleRate, PITCH_OPTIONS);
        this.clarity = result.clarity;
        // The median of the last three estimates drops single wrong ones.
        const estimate = result.frequency;
        this.recent.shift();
        this.recent.push(estimate);
        const sorted = [...this.recent].sort((a, b) => a - b);
        this.pitch = estimate > 0 ? (sorted[1] > 0 ? sorted[1] : estimate) : 0;
    }

    // Each row is its loudest bin, or the bin under it when narrower than a bin.
    spectrumRows() {
        const { db, edges, rows } = this;
        const last = this.size / 2 - 1;
        for (let r = 0; r < ROWS; r++) {
            const from = edges[r], to = edges[r + 1];
            let level;
            if (to - from < 1) {
                const middle = Math.min(last - 1, (from + to) / 2);
                const k = Math.floor(middle), t = middle - k;
                level = db[k] * (1 - t) + db[k + 1] * t;
            } else {
                level = -180;
                const end = Math.min(Math.ceil(to), last + 1);
                for (let k = Math.floor(from); k < end; k++) if (db[k] > level) level = db[k];
            }
            const value = Math.round((level - MIN_DB) / -MIN_DB * 255);
            rows[r] = value < 0 ? 0 : value > 255 ? 255 : value;
        }
    }

    // The loudest bin between 50 Hz and 5 kHz, refined with a parabola.
    findDominant() {
        const db = this.db, binHz = this.sampleRate / this.size;
        const first = Math.max(1, Math.ceil(MIN_FREQUENCY / binHz));
        const last = Math.min(this.size / 2 - 2, Math.floor(5000 / binHz));
        let best = first;
        for (let k = first + 1; k <= last; k++) if (db[k] > db[best]) best = k;
        if (db[best] < -55) {
            this.dominant = 0;
            return;
        }
        const a = db[best - 1], b = db[best], c = db[best + 1];
        const denominator = a - 2 * b + c;
        const delta = denominator !== 0 ? Math.max(-0.5, Math.min(0.5, 0.5 * (a - c) / denominator)) : 0;
        this.dominant = (best + delta) * binHz;
    }
}
