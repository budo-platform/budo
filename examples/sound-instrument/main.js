/// <reference path="./budo.d.ts" />

sys.audio.setMasterGain(1);

let freq = 440;
let gain = 0.0;

let targetFreq = 0;
let targetGain = 0;

const osc = sys.audio.createOscillator();
sys.audio.setOscillatorType(osc, 'sawtooth');
sys.audio.setOscillatorFrequency(osc, freq);
sys.audio.setOscillatorGain(osc, gain);
sys.audio.startOscillator(osc);

const speed = 40;

function updateOscillator() {
    if (freq == targetFreq && gain == targetGain)
        return;

    gain = (speed * gain + targetGain) / (speed + 1);
    freq = (speed * freq + targetFreq) / (speed + 1);

    sys.audio.setOscillatorGain(osc, gain);
    sys.audio.setOscillatorFrequency(osc, freq);
}

function frame(timestamp) {
    sys.canvas.clear("#222831");
    sys.canvas.setFillColor("#eeeeee");
    sys.canvas.drawText("Press touch to play the instrument", 24, 48, 28);

    const i = sys.input.get();
    if (i.pointer.down) {
        targetGain = 1 - i.pointer.y / sys.window.getHeight();
        targetFreq = 880 * i.pointer.x / sys.window.getWidth();

        sys.canvas.setFillColor("#ff0000");
        sys.canvas.drawText(`Freq: ${targetFreq.toFixed(2)} Gain: ${targetGain.toFixed(2)}`, i.pointer.x, i.pointer.y, 28);
    } else {
        targetGain = 0;
        targetFreq = 0;
    }

    updateOscillator();

    sys.canvas.setFillColor("#bc11e7");
    const x = freq * sys.window.getWidth() / 880;
    const y = (1 - gain) * sys.window.getHeight();
    sys.canvas.drawCircle(x, y, 20 * gain);

    sys.animation.requestFrame(frame);
}

sys.animation.requestFrame(frame);
