const MODEL_PATH = 'files/models/smollm2-135m-instruct-q2_k.gguf';
const MODEL_URL = 'https://huggingface.co/bartowski/SmolLM2-135M-Instruct-GGUF/resolve/main/SmolLM2-135M-Instruct-Q2_K.gguf?download=true';
const MODEL_SIZE = 88202080;
const DOWNLOAD_CHUNK_SIZE = 4 * 1024 * 1024;
const MODEL_CONTEXT_SIZE = 8192;
const SDL_ENTER = 40;
const SDL_BACKSPACE = 42;
const SDL_RIGHT = 79;
const SDL_LEFT = 80;

interface ChatLine {
    role: 'you' | 'tiny model' | 'status';
    text: string;
}

interface ButtonRect {
    x: number;
    y: number;
    width: number;
    height: number;
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

let phase: 'checking' | 'downloading' | 'loading' | 'ready' | 'generating' | 'error' = 'checking';
let statusText = 'Checking local model...';
let errorText = '';
let inputText = '';
let inputCaret = 0;
let compositionText = '';
let inputFocused = false;
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
    errorText = message;
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
    errorText = '';
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
    if (inputFocused) return;
    inputFocused = true;
    inputCaret = inputText.length;
    sys.input.startTextInput({
        text: inputText,
        selectionStart: inputCaret,
        selectionEnd: inputCaret,
        multiline: false,
    });
}

function blurInput(): void {
    if (inputFocused) sys.input.stopTextInput();
    inputFocused = false;
    compositionText = '';
}

function handleTextInput(input: InputState): void {
    if (!inputFocused) return;
    if (input.textEdit) {
        inputText = input.textEdit.text.slice(0, 400);
        inputCaret = Math.max(0, Math.min(inputText.length, input.textEdit.selectionEnd));
    } else if (input.text && inputText.length < 400) {
        const inserted = input.text.slice(0, 400 - inputText.length);
        inputText = inputText.slice(0, inputCaret) + inserted + inputText.slice(inputCaret);
        inputCaret += inserted.length;
    }
    if (input.composition.changed) {
        compositionText = input.composition.active ? input.composition.text : '';
    }
}

function handleEditingKeys(input: InputState): void {
    if (!inputFocused || input.textEdit) return;
    if (sys.input.isKeyPressed(SDL_BACKSPACE) && inputCaret > 0) {
        inputText = inputText.slice(0, inputCaret - 1) + inputText.slice(inputCaret);
        inputCaret = inputCaret - 1;
        compositionText = '';
    }
    if (sys.input.isKeyPressed(SDL_LEFT)) {
        inputCaret = Math.max(0, inputCaret - 1);
    }
    if (sys.input.isKeyPressed(SDL_RIGHT)) {
        inputCaret = Math.min(inputText.length, inputCaret + 1);
    }
}

function ask(): void {
    const prompt = inputText.trim();
    if (!chat || phase !== 'ready' || !prompt) return;
    lines.push({ role: 'you', text: prompt });
    lines.push({ role: 'tiny model', text: '' });
    inputText = '';
    inputCaret = 0;
    compositionText = '';
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
    errorText = '';
    if (modelExists()) loadModel();
    else downloadModel();
}

function contains(rect: ButtonRect, x: number, y: number): boolean {
    return x >= rect.x && x <= rect.x + rect.width && y >= rect.y && y <= rect.y + rect.height;
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

function drawButton(rect: ButtonRect, label: string, enabled: boolean, primary: boolean): void {
    let fill = '#E5E8E5';
    let stroke = colors.border;
    let text = colors.muted;
    if (enabled) {
        fill = primary ? colors.accent : colors.panel;
        stroke = primary ? colors.accent : colors.border;
        text = primary ? '#FFFFFF' : colors.ink;
    }
    sys.canvas.setFillColor(fill);
    sys.canvas.drawRoundRect(rect.x, rect.y, rect.width, rect.height, 6, 6);
    sys.canvas.setStrokeColor(stroke);
    sys.canvas.setStrokeWidth(1);
    sys.canvas.drawRoundRect(rect.x, rect.y, rect.width, rect.height, 6, 6);
    const size = 18;
    const textWidth = sys.canvas.measureText(label, size);
    sys.canvas.setFillColor(text);
    sys.canvas.drawText(label, rect.x + (rect.width - textWidth) / 2, rect.y + 27, size);
}

function frame(): void {
    const width = sys.window.getWidth();
    const height = sys.window.getHeight();
    const density = Math.max(1, sys.window.getDisplayDensity());
    const margin = Math.max(16, Math.round(18 * density));
    const headerHeight = 92;
    const composerHeight = 92;
    const contentTop = headerHeight;
    const contentBottom = height - composerHeight;
    const input = sys.input.get();
    handleTextInput(input);
    handleEditingKeys(input);

    if (phase === 'generating') {
        const elapsed = Math.max(0, input.totalTime - generationStartedAt);
        const dots = '.'.repeat(Math.floor(elapsed * 2) % 4);
        statusText = lines[lines.length - 1].text
            ? `Streaming locally · ${elapsed.toFixed(1)}s · ${lines[lines.length - 1].text.length} characters`
            : `Model is thinking${dots} · ${elapsed.toFixed(1)}s`;
    }

    sys.canvas.clear(colors.background);
    sys.canvas.setFillColor(colors.ink);
    sys.canvas.drawText('Tiny Local LLM', margin, 36, 30);
    sys.canvas.setFillColor(colors.muted);
    sys.canvas.drawText(statusText, margin, 68, 17);

    const devices = sys.llamacpp.getDevices();
    const deviceText = devices.map(function (device): string { return device.backend; }).join(' · ');
    const deviceWidth = sys.canvas.measureText(deviceText, 15);
    sys.canvas.drawText(deviceText, Math.max(margin, width - margin - deviceWidth), 36, 15);

    sys.canvas.save();
    sys.canvas.clipRect(0, contentTop, width, Math.max(0, contentBottom - contentTop));
    const transcriptWidth = width - margin * 2;
    const availableHeight = Math.max(0, contentBottom - contentTop);
    let y = Math.min(contentTop + 16,
        contentBottom - transcriptHeight(transcriptWidth) - 12);
    for (const line of lines) {
        const roleColor = line.role === 'you' ? colors.accent : line.role === 'status' ? colors.warning : colors.ink;
        sys.canvas.setFillColor(roleColor);
        sys.canvas.drawText(line.role.toUpperCase(), margin, y + 16, 13);
        y += 28;
        const wrapped = wrapText(line.text || (phase === 'generating' ? 'Thinking...' : ''), transcriptWidth, 20);
        sys.canvas.setFillColor(line.role === 'status' ? colors.muted : colors.ink);
        for (const textLine of wrapped) {
            sys.canvas.drawText(textLine, margin, y + 19, 20);
            y += 28;
        }
        y += 18;
    }
    sys.canvas.restore();

    if (phase === 'downloading') {
        const elapsed = Math.max(0, sys.input.get().totalTime - downloadStartedAt);
        const pulse = 0.25 + 0.55 * (0.5 + 0.5 * Math.sin(elapsed * 4));
        sys.canvas.setAlpha(Math.round(255 * pulse));
        sys.canvas.setFillColor(colors.accent);
        sys.canvas.drawRect(margin, headerHeight - 5, Math.max(40, (width - margin * 2) * 0.35), 3);
        sys.canvas.setAlpha(255);
    }
    if (phase === 'generating') {
        const bannerWidth = Math.min(360, width - margin * 2);
        const bannerX = (width - bannerWidth) / 2;
        const bannerY = contentBottom - 58;
        sys.canvas.setFillColor(colors.accentSoft);
        sys.canvas.drawRoundRect(bannerX, bannerY, bannerWidth, 42, 6, 6);
        sys.canvas.setFillColor(colors.accent);
        const bannerText = lines[lines.length - 1].text ? 'Streaming response...' : 'Running model...';
        const bannerTextWidth = sys.canvas.measureText(bannerText, 18);
        sys.canvas.drawText(bannerText, bannerX + (bannerWidth - bannerTextWidth) / 2,
            bannerY + 27, 18);
    }

    const inputRect: ButtonRect = { x: margin, y: height - 70, width: Math.max(120, width - margin * 2 - 220), height: 48 };
    const actionRect: ButtonRect = { x: inputRect.x + inputRect.width + 10, y: inputRect.y, width: 100, height: 48 };
    const clearRect: ButtonRect = { x: actionRect.x + 108, y: inputRect.y, width: 100, height: 48 };

    sys.canvas.setFillColor(colors.panel);
    sys.canvas.drawRoundRect(inputRect.x, inputRect.y, inputRect.width, inputRect.height, 6, 6);
    sys.canvas.setStrokeColor(inputFocused ? colors.accent : colors.border);
    sys.canvas.setStrokeWidth(inputFocused ? 2 : 1);
    sys.canvas.drawRoundRect(inputRect.x, inputRect.y, inputRect.width, inputRect.height, 6, 6);

    const displayText = inputText || (phase === 'ready' ? 'Ask the local assistant...' : 'Waiting for model...');
    const textColor = inputText ? colors.ink : colors.muted;
    const textX = inputRect.x + 14;
    const baseline = inputRect.y + 31;
    sys.canvas.setFillColor(textColor);
    sys.canvas.save();
    sys.canvas.clipRect(inputRect.x + 4, inputRect.y + 4, inputRect.width - 8, inputRect.height - 8);
    sys.canvas.drawText(displayText, textX, baseline, 20);
    if (compositionText) {
        const before = inputText.slice(0, inputCaret);
        const compositionX = textX + sys.canvas.measureText(before, 20);
        sys.canvas.setStrokeColor(colors.accent);
        sys.canvas.drawLine(compositionX, baseline + 4, compositionX + sys.canvas.measureText(compositionText, 20), baseline + 4);
    }
    if (inputFocused && Math.floor(input.totalTime * 2) % 2 === 0) {
        const caretX = textX + sys.canvas.measureText(inputText.slice(0, inputCaret), 20);
        sys.canvas.setStrokeColor(colors.ink);
        sys.canvas.drawLine(caretX, inputRect.y + 10, caretX, inputRect.y + 38);
    }
    sys.canvas.restore();

    const canAsk = phase === 'ready' && inputText.trim().length > 0;
    drawButton(actionRect, phase === 'generating' ? 'Stop' : phase === 'error' ? 'Retry' : 'Send',
        phase === 'generating' || phase === 'error' || canAsk, true);
    drawButton(clearRect, 'Clear', chat !== null && phase !== 'generating', false);

    const mousePressed = input.mouse.leftPressed;
    if (mousePressed) {
        if (contains(inputRect, input.mouse.x, input.mouse.y) && phase === 'ready') focusInput();
        else if (contains(actionRect, input.mouse.x, input.mouse.y)) {
            if (phase === 'generating') cancelGeneration();
            else if (phase === 'error') retry();
            else ask();
        } else if (contains(clearRect, input.mouse.x, input.mouse.y) && phase !== 'generating') clearChat();
    }
    if (!mousePressed) {
        for (const pointer of input.pointers) {
            if (!pointer.pressed) continue;
            if (contains(inputRect, pointer.x, pointer.y) && phase === 'ready') focusInput();
            else if (contains(actionRect, pointer.x, pointer.y)) {
                if (phase === 'generating') cancelGeneration();
                else if (phase === 'error') retry();
                else ask();
            } else if (contains(clearRect, pointer.x, pointer.y) && phase !== 'generating') clearChat();
            break;
        }
    }

    if (inputFocused && phase === 'ready' && sys.input.isKeyPressed(SDL_ENTER)) {
        ask();
    }

    if (inputFocused) {
        const caretX = textX + sys.canvas.measureText(inputText.slice(0, inputCaret), 20);
        sys.input.updateTextInput({
            text: inputText,
            selectionStart: inputCaret,
            selectionEnd: inputCaret,
            multiline: false,
            caret: { x: caretX, y: inputRect.y + 8, width: 1, height: 32 },
        });
    }

    sys.animation.requestFrame(frame);
}

start();
sys.animation.requestFrame(frame);
