/// <reference path="../../budo.d.ts" />

import { createUI } from './ui-library.js';

const MODEL_PATH = 'files/models/smollm2-135m-instruct-q2_k.gguf';
const MODEL_URL = 'https://huggingface.co/bartowski/SmolLM2-135M-Instruct-GGUF/resolve/main/SmolLM2-135M-Instruct-Q2_K.gguf?download=true';
const MODEL_SIZE = 88202080;
const DOWNLOAD_CHUNK_SIZE = 4 * 1024 * 1024;
const MODEL_CONTEXT_SIZE = 8192;
const SDL_ENTER = 40;

interface ChatLine {
    role: 'you' | 'tiny model' | 'status';
    text: string;
}

const colors = {
    background: '#F2F4F1',
    panel: '#FFFFFF',
    ink: '#17211B',
    muted: '#667169',
    border: '#CDD4CE',
    accent: '#176B4D',
    accentSoft: '#DDECE5',
    warning: '#9A5A13',
    error: '#A2392E',
};
const ui = createUI({
    background: colors.background, surface: colors.panel, raised: '#E5E8E5',
    ink: colors.ink, muted: colors.muted, border: colors.border,
    accent: colors.accent, accentInk: '#FFFFFF', highlight: colors.accent,
    font: 20, radius: 6,
});
const promptModel = { value: '' };

let phase: 'checking' | 'downloading' | 'loading' | 'ready' | 'generating' | 'error' = 'checking';
let statusText = 'Checking local model...';
let pendingFocus = false;
let downloadStartedAt = 0;
let downloadedBytes = 0;
let generationStartedAt = 0;
let model: LlamaCppModel | null = null;
let chat: LlamaCppChat | null = null;
let generation: LlamaCppGeneration | null = null;
let lines: ChatLine[] = [
    { role: 'status', text: 'SmolLM2-135M-Instruct runs fully locally after its one-time 88 MB download.' },
];

function setError(message: string): void {
    phase = 'error';
    statusText = message;
    sys.log(message);
}

function modelExists(): boolean {
    return sys.files.exists(MODEL_PATH) && sys.files.size(MODEL_PATH) === MODEL_SIZE;
}

function loadModel(): void {
    phase = 'loading';
    statusText = 'Loading the local model...';
    sys.llamacpp.loadModel(MODEL_PATH, {
        contextSize: MODEL_CONTEXT_SIZE,
        device: 'auto',
        gpuLayers: 'auto',
        useMmap: true,
        allowFallback: true,
    }, function (loadedModel, error): void {
        if (!loadedModel) {
            setError(error ? error.message : sys.llamacpp.getError());
            return;
        }
        model = loadedModel;
        const info = model.getInfo();
        chat = model.createChat({
            systemPrompt: 'You are a concise and friendly local assistant. Answer in at most four short sentences.',
            contextSize: MODEL_CONTEXT_SIZE,
        });
        phase = 'ready';
        statusText = `${info.backend} · ${info.gpuLayers}/${info.totalLayers} GPU layers · ready`;
        focusInput();
    });
}

function downloadChunk(start: number): void {
    const end = Math.min(MODEL_SIZE - 1, start + DOWNLOAD_CHUNK_SIZE - 1);
    fetch(MODEL_URL, { headers: { Range: `bytes=${start}-${end}` } })
        .then(function (response): void {
            if (response.status !== 206) {
                throw new Error(`Range download failed: HTTP ${response.status}`);
            }
            const bytes = response.arrayBuffer();
            if (bytes.byteLength !== end - start + 1) {
                throw new Error(`Unexpected chunk size: ${bytes.byteLength} bytes`);
            }
            if (start === 0) sys.files.writeBinary(MODEL_PATH, bytes);
            else sys.files.appendBinary(MODEL_PATH, bytes);
            downloadedBytes = end + 1;
            statusText = `Downloading chat model... ${Math.floor(downloadedBytes * 100 / MODEL_SIZE)}%`;
            if (downloadedBytes < MODEL_SIZE) downloadChunk(downloadedBytes);
            else if (sys.files.size(MODEL_PATH) === MODEL_SIZE) loadModel();
            else throw new Error('Downloaded model size does not match expected size');
        })
        .catch(function (error): void {
            setError(String(error));
        });
}

function downloadModel(): void {
    phase = 'downloading';
    downloadedBytes = 0;
    statusText = 'Downloading 88 MB chat model... 0%';
    downloadStartedAt = sys.input.get().totalTime;
    downloadChunk(0);
}

function start(): void {
    if (!sys.capabilities.llamacpp.available || typeof sys.llamacpp === 'undefined') {
        setError('This Budo build does not include llama.cpp. Rebuild with ENABLE_LLAMACPP=ON.');
        return;
    }
    if (modelExists()) loadModel();
    else downloadModel();
}

function focusInput(): void {
    pendingFocus = true;
}

function ask(): void {
    const prompt = promptModel.value.trim();
    if (!chat || phase !== 'ready' || !prompt) return;
    lines.push({ role: 'you', text: prompt });
    lines.push({ role: 'tiny model', text: '' });
    promptModel.value = '';
    phase = 'generating';
    statusText = 'Generating locally...';
    generationStartedAt = sys.input.get().totalTime;
    sys.log(`Prompt queued: ${prompt}`);
    try {
        generation = chat.send(prompt, {
            maxTokens: MODEL_CONTEXT_SIZE,
            minTokens: 32,
            temperature: 0.7,
            topK: 32,
            topP: 0.9,
            repetitionPenalty: 1.08,
            stop: ['<|im_end|>', '\n\nUser:'],
        }, {
            onText: function (chunk): void {
                lines[lines.length - 1].text += chunk;
            },
            onComplete: function (result): void {
                phase = 'ready';
                generation = null;
                statusText = `${result.finishReason} · ${result.generatedTokens} tokens · ${result.generatedTokensPerSecond.toFixed(1)} tok/s`;
                sys.log(`Generation complete: ${result.generatedTokens} tokens, ${result.generatedTokensPerSecond.toFixed(1)} tok/s`);
                focusInput();
            },
            onError: function (error): void {
                generation = null;
                lines[lines.length - 1].text = `Error: ${error.message}`;
                setError(error.message);
            },
        });
    } catch (error) {
        generation = null;
        const message = String(error);
        lines[lines.length - 1].text = `Error: ${message}`;
        setError(message);
    }
}

function cancelGeneration(): void {
    if (generation) generation.cancel();
}

function clearChat(): void {
    if (chat) chat.clear();
    lines = [{ role: 'status', text: 'Conversation cleared. The model remains loaded locally.' }];
}

function retry(): void {
    if (modelExists()) loadModel();
    else downloadModel();
}

function wrapText(text: string, width: number, size: number): string[] {
    const paragraphs = text.split('\n');
    const output: string[] = [];
    for (const paragraph of paragraphs) {
        const words = paragraph.split(/\s+/).filter(function (word): boolean { return word.length > 0; });
        if (words.length === 0) {
            output.push('');
            continue;
        }
        let line = '';
        for (const word of words) {
            const candidate = line ? `${line} ${word}` : word;
            if (line && sys.canvas.measureText(candidate, size) > width) {
                output.push(line);
                line = word;
            } else {
                line = candidate;
            }
        }
        if (line) output.push(line);
    }
    return output;
}

function transcriptHeight(width: number): number {
    let height = 16;
    for (const line of lines) {
        const content = line.text || (phase === 'generating' ? 'Thinking...' : '');
        height += 28 + wrapText(content, width, 20).length * 28 + 18;
    }
    return height;
}

function frame(): void {
    const width = sys.window.getWidth();
    const height = sys.window.getHeight();
    const density = Math.max(1, sys.window.getDisplayDensity());
    const margin = Math.max(16, Math.round(18 * density));
    const input = sys.input.get();

    if (phase === 'generating') {
        const elapsed = Math.max(0, input.totalTime - generationStartedAt);
        const dots = '.'.repeat(Math.floor(elapsed * 2) % 4);
        statusText = lines[lines.length - 1].text
            ? `Streaming locally · ${elapsed.toFixed(1)}s · ${lines[lines.length - 1].text.length} characters`
            : `Model is thinking${dots} · ${elapsed.toFixed(1)}s`;
    }

    sys.canvas.clear(colors.background);
    ui.begin(input, { x: 0, y: 0, width, height });
    const page = ui.inset({ x: 0, y: 0, width, height }, margin);
    const compact = width < 600;
    const [header, transcript, composer] = ui.rows(page,
        [64, { weight: 1 }, compact ? 106 : 52], 12);

    ui.label('Tiny Local LLM', { ...header, height: 36 }, { size: 30 });
    sys.canvas.save();
    sys.canvas.clipRect(header.x, header.y + 38, header.width, 26);
    ui.label(statusText, { x: header.x, y: header.y + 38, width: header.width, height: 26 },
        { size: 17, color: phase === 'error' ? colors.error : colors.muted });
    sys.canvas.restore();
    if (sys.llamacpp && width >= 720) {
        const devices = sys.llamacpp.getDevices();
        const deviceText = devices.map(function (device): string { return device.backend; }).join(' · ');
        const deviceWidth = sys.canvas.measureText(deviceText, 15);
        ui.label(deviceText, { x: Math.max(header.x, header.x + header.width - deviceWidth),
            y: header.y, width: deviceWidth, height: 28 }, { size: 15, color: colors.muted });
    }

    const transcriptWidth = Math.max(0, transcript.width - 16);
    ui.scroll('transcript', transcript, transcriptHeight(transcriptWidth), content => {
        let y = content.y + 16;
        for (const line of lines) {
            const roleColor = line.role === 'you' ? colors.accent : line.role === 'status' ? colors.warning : colors.ink;
            ui.text(line.role.toUpperCase(), content.x, y + 16, 13, roleColor);
            y += 28;
            const wrapped = wrapText(line.text || (phase === 'generating' ? 'Thinking...' : ''), transcriptWidth, 20);
            for (const textLine of wrapped) {
                ui.text(textLine, content.x, y + 19, 20, line.role === 'status' ? colors.muted : colors.ink);
                y += 28;
            }
            y += 18;
        }
    }, { followEnd: true });

    if (phase === 'downloading') {
        const elapsed = Math.max(0, input.totalTime - downloadStartedAt);
        const pulse = 0.25 + 0.55 * (0.5 + 0.5 * Math.sin(elapsed * 4));
        sys.canvas.setAlpha(Math.round(255 * pulse));
        ui.fill({ x: header.x, y: header.y + header.height + 4,
            width: Math.max(40, header.width * 0.35), height: 3 }, colors.accent, 1);
        sys.canvas.setAlpha(255);
    }
    if (phase === 'generating') {
        const banner = { x: transcript.x, y: transcript.y + transcript.height - 38,
            width: transcript.width, height: 38 };
        ui.fill(banner, colors.accentSoft);
        ui.label(lines[lines.length - 1].text ? 'Streaming response...' : 'Running model...',
            ui.inset(banner, 8), { size: 18, color: colors.accent });
    }

    let inputRect;
    let actionRect;
    let clearRect;
    if (compact) {
        const [fieldRow, buttonsRow] = ui.rows(composer, [48, 48], 10);
        inputRect = fieldRow;
        [actionRect, clearRect] = ui.columns(buttonsRow, [{ weight: 1 }, { weight: 1 }], 10);
    } else {
        [inputRect, actionRect, clearRect] = ui.columns(composer, [{ weight: 1 }, 90, 90], 10);
    }
    if (phase === 'ready') {
        if (pendingFocus) {
            ui.focusField('prompt', promptModel);
            pendingFocus = false;
        }
        ui.field('prompt', inputRect, promptModel, { placeholder: 'Ask the local assistant...', maxLength: 400 });
    } else {
        ui.fill(inputRect, colors.panel);
        ui.label(phase === 'generating' ? 'Generating locally...' : 'Waiting for model...',
            ui.inset(inputRect, 12), { color: colors.muted });
    }
    const canAsk = phase === 'ready' && promptModel.value.trim().length > 0;
    if (ui.button('action', phase === 'generating' ? 'Stop' : phase === 'error' ? 'Retry' : 'Send',
        actionRect, { primary: true, enabled: phase === 'generating' || phase === 'error' || canAsk })) {
        if (phase === 'generating') cancelGeneration();
        else if (phase === 'error') retry();
        else ask();
    }
    if (ui.button('clear', 'Clear', clearRect, { enabled: chat !== null && phase !== 'generating' }))
        clearChat();
    if (ui.focus === 'prompt' && phase === 'ready' && sys.input.isKeyPressed(SDL_ENTER)) ask();
    ui.end();
    sys.animation.requestFrame(frame);
}

start();
sys.animation.requestFrame(frame);
