// Engine and surface sound from a handful of oscillators:
//   - the player's engine: sawtooth + square sub-octave + triangle overtone,
//     pitched by a five-gear rev model, revving freely in the air;
//   - sand: noise for sliding and wind, a low rough square for the ground;
//   - a sine thump on landing;
//   - the nearest rival's engine, with an exaggerated Doppler shift.
// start() is called on an input gesture (web browsers require one), and may
// be called again after a failure.

const GEAR_TOPS = [11, 21, 32, 44, 58]; // world units per second
const SPEED_OF_SOUND = 90;              // exaggerated so pass-bys swoop
const RIVAL_RANGE = 70;

function clamp01(x) { return Math.max(0, Math.min(1, x)); }
function approach(current, target, rate, dt) {
    return current + (target - current) * (1 - Math.exp(-rate * dt));
}

// Normalized revs 0..1 for a given speed; past the last gear, revs stay high.
function revsForSpeed(speed) {
    let low = 0;
    for (let gear = 0; gear < GEAR_TOPS.length; gear++) {
        const high = GEAR_TOPS[gear];
        if (speed < high || gear === GEAR_TOPS.length - 1) {
            const t = clamp01((speed - low) / (high - low));
            return gear === 0 ? 0.12 + t * 0.88 : 0.38 + t * 0.62;
        }
        low = high;
    }
    return 1;
}

function engineHz(revs) { return 36 + revs * 104; }

export function createEngineAudio() {
    let ready = false;
    let muted = false;
    let paused = false; // photo mode: silent, the settings kept
    let voices = null;
    let playerRevs = 0.1;
    let rivalRevs = 0.1;
    let thumpHz = 60;

    function makeVoice(type) {
        const osc = sys.audio.createOscillator();
        if (osc < 0) throw new Error('no oscillator');
        sys.audio.setOscillatorType(osc, type);
        sys.audio.setOscillatorGain(osc, 0);
        return osc;
    }

    function start() {
        if (ready) return;
        const created = [];
        try {
            sys.audio.start();
            const make = (type) => { const osc = makeVoice(type); created.push(osc); return osc; };
            const v = {
                body: make('sawtooth'),
                sub: make('square'),
                whine: make('triangle'),
                noise: make('noise'),
                ground: make('square'),
                thump: make('sine'),
                rival: make('sawtooth'),
            };
            for (const key of ['body', 'sub', 'whine', 'noise', 'ground', 'rival']) {
                sys.audio.setOscillatorFrequency(v[key], 60);
                sys.audio.startOscillator(v[key]);
            }
            sys.audio.setOscillatorEnvelope(v.thump, 0.004, 0.32, 0.0, 0.05);
            voices = v;
            ready = true;
            applyGain();
        } catch (e) {
            // Leave nothing half-made; a later gesture may retry.
            for (const osc of created) sys.audio.destroyOscillator(osc);
            sys.log(`engine audio unavailable: ${e && e.message ? e.message : e}`);
        }
    }

    function applyGain() {
        if (ready) sys.audio.setMasterGain(muted || paused ? 0 : 0.7);
    }

    function setMuted(value) {
        muted = value;
        applyGain();
    }

    function setPaused(value) {
        paused = value;
        applyGain();
    }

    // player: the player's car; throttle: -1..1; rival: nearest AI car or null.
    // landing: 0..1 impact strength this frame.
    function update(dt, player, throttle, rival, landing) {
        if (!ready || dt <= 0) return;
        const v = voices;
        const speed = Math.hypot(player.vx, player.vy);
        const load = Math.abs(throttle);

        // Free revs in the air with the throttle down, else the gearbox.
        const targetRevs = player.grounded
            ? Math.max(revsForSpeed(speed), load * 0.22)
            : (load > 0.1 ? 0.95 : 0.3);
        playerRevs = approach(playerRevs, targetRevs, player.grounded ? 9 : 4, dt);
        const hz = engineHz(playerRevs);
        const rough = (Math.random() - 0.5) * 14; // cents of firing jitter
        sys.audio.setOscillatorFrequency(v.body, hz);
        sys.audio.setOscillatorDetune(v.body, rough);
        sys.audio.setOscillatorFrequency(v.sub, hz * 0.5);
        sys.audio.setOscillatorFrequency(v.whine, hz * 2.01);
        sys.audio.setOscillatorGain(v.body, 0.07 + 0.06 * load);
        sys.audio.setOscillatorGain(v.sub, 0.05 + 0.03 * load);
        sys.audio.setOscillatorGain(v.whine, 0.015 + 0.035 * playerRevs * playerRevs);

        // Sand: sliding hiss, wind at speed, and ground rumble.
        const cosH = Math.cos(player.heading), sinH = Math.sin(player.heading);
        const slip = clamp01(Math.abs(player.vx * sinH - player.vy * cosH) / 7);
        const fast = clamp01((speed - 12) / 35);
        const onGround = player.grounded ? 1 : 0;
        sys.audio.setOscillatorGain(v.noise,
            onGround * slip * clamp01(speed / 10) * 0.055 + fast * 0.018);
        sys.audio.setOscillatorFrequency(v.ground, 28 + Math.random() * 34);
        sys.audio.setOscillatorGain(v.ground, onGround * clamp01(speed / 25) * 0.03);

        if (landing > 0.05) {
            thumpHz = 74;
            sys.audio.setOscillatorFrequency(v.thump, thumpHz);
            sys.audio.setOscillatorGain(v.thump, 0.18 + 0.32 * landing);
            sys.audio.noteOn(v.thump);
        } else if (thumpHz > 38) {
            thumpHz = Math.max(38, thumpHz - 110 * dt);
            sys.audio.setOscillatorFrequency(v.thump, thumpHz);
        }

        // The nearest rival, Doppler-shifted by its speed relative to us.
        if (rival) {
            const dx = rival.x - player.x, dy = rival.y - player.y;
            const dist = Math.hypot(dx, dy);
            const ux = dist > 0.01 ? dx / dist : 0, uy = dist > 0.01 ? dy / dist : 0;
            const receding = (rival.vx - player.vx) * ux + (rival.vy - player.vy) * uy;
            const doppler = SPEED_OF_SOUND / Math.max(30, SPEED_OF_SOUND + receding);
            rivalRevs = approach(rivalRevs, revsForSpeed(Math.hypot(rival.vx, rival.vy)), 8, dt);
            sys.audio.setOscillatorFrequency(v.rival, engineHz(rivalRevs) * 1.07 * doppler);
            const near = clamp01(1 - dist / RIVAL_RANGE);
            sys.audio.setOscillatorGain(v.rival, near * near * 0.06);
        } else {
            sys.audio.setOscillatorGain(v.rival, 0);
        }
    }

    return {
        start,
        update,
        setMuted,
        setPaused,
        get ready() { return ready; },
        get muted() { return muted; },
    };
}
