interface Rect {
    x: number;
    y: number;
    width: number;
    height: number;
}

interface PointerState {
    x: number;
    y: number;
    down: boolean;
    pressed: boolean;
}

interface HuffmanNode {
    weight: number;
    members: number[];
}

const COLORS = {
    paper: '#F3F0E8',
    ink: '#17211B',
    muted: '#68726C',
    rule: '#C9C8BE',
    red: '#E85D3F',
    yellow: '#E7B647',
    teal: '#70A99A',
    blue: '#426B8A',
    white: '#FFFFFF'
};

const CHAPTERS = ['A BIT', 'SURPRISE', 'ENTROPY', 'ALPHABETS', 'SAMPLING', 'CODING', 'MODEL COST'];
const TITLES = [
    'Begin with a choice',
    'Rare events say more',
    'Average the surprise',
    'Uncertainty over an alphabet',
    'The source is not the sample',
    'Entropy sets the compression limit',
    'Wrong predictions cost bits'
];

let chapter: number = 0;
let probability: number = 0.5;
let modelProbability: number = 0.5;
let distributionShape: number = 0.25;
let activeControl: string = '';
let pointerWasDown: boolean = false;
let density: number = 1;
let randomState: number = 0x5f3759df;
let observations: number[] = [];

function clamp(value: number, low: number, high: number): number {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

function log2(value: number): number {
    return Math.log(value) / Math.log(2);
}

function information(value: number): number {
    if (value <= 0) return 0;
    return -log2(value);
}

function binaryEntropy(value: number): number {
    if (value <= 0 || value >= 1) return 0;
    return -value * log2(value) - (1 - value) * log2(1 - value);
}

function distribution(): number[] {
    const remainder: number = (1 - distributionShape) / 3;
    return [distributionShape, remainder, remainder, remainder];
}

function entropy(values: number[]): number {
    let total: number = 0;
    for (let index: number = 0; index < values.length; index += 1) {
        if (values[index] > 0) total -= values[index] * log2(values[index]);
    }
    return total;
}

function crossEntropy(actual: number, model: number): number {
    const bounded: number = clamp(model, 0.001, 0.999);
    return -actual * log2(bounded) - (1 - actual) * log2(1 - bounded);
}

function randomUnit(): number {
    randomState = (randomState * 1664525 + 1013904223) >>> 0;
    return randomState / 4294967296;
}

function drawObservation(): void {
    observations.push(randomUnit() < probability ? 1 : 0);
    if (observations.length > 128) observations.shift();
}

function drawObservationBatch(count: number): void {
    for (let index: number = 0; index < count; index += 1) drawObservation();
}

function format(value: number, digits: number): string {
    return value.toFixed(digits);
}

function contains(rect: Rect, x: number, y: number): boolean {
    return x >= rect.x && x <= rect.x + rect.width && y >= rect.y && y <= rect.y + rect.height;
}

function fillRect(rect: Rect, color: string): void {
    sys.canvas.setFillColor(color);
    sys.canvas.drawRect(rect.x, rect.y, rect.width, rect.height);
}

function strokeRect(rect: Rect, color: string, width: number): void {
    sys.canvas.setStrokeColor(color);
    sys.canvas.setStrokeWidth(width);
    sys.canvas.drawRect(rect.x, rect.y, rect.width, rect.height);
}

function drawRule(x1: number, y: number, x2: number, color: string, width: number): void {
    sys.canvas.setStrokeColor(color);
    sys.canvas.setStrokeWidth(width);
    sys.canvas.drawLine(x1, y, x2, y);
}

function drawVerticalRule(x: number, y1: number, y2: number, color: string, width: number): void {
    sys.canvas.setStrokeColor(color);
    sys.canvas.setStrokeWidth(width);
    sys.canvas.drawLine(x, y1, x, y2);
}

function wrapText(text: string, maxWidth: number, size: number): string[] {
    const words: string[] = text.split(' ');
    const lines: string[] = [];
    let line: string = '';
    for (let index: number = 0; index < words.length; index += 1) {
        let candidate: string = words[index];
        if (line.length > 0) candidate = line + ' ' + words[index];
        if (line.length > 0 && sys.canvas.measureText(candidate, size) > maxWidth) {
            lines.push(line);
            line = words[index];
        } else {
            line = candidate;
        }
    }
    if (line.length > 0) lines.push(line);
    return lines;
}

function drawParagraph(text: string, x: number, y: number, width: number, size: number, color: string, leading: number): number {
    const lines: string[] = wrapText(text, width, size);
    sys.canvas.setFillColor(color);
    for (let index: number = 0; index < lines.length; index += 1) {
        sys.canvas.drawText(lines[index], x, y + index * leading, size);
    }
    return y + lines.length * leading;
}

function drawLabel(text: string, x: number, y: number, color: string): void {
    sys.canvas.setFillColor(color);
    sys.canvas.drawText(text, x, y, 11 * density);
}

function drawCentered(text: string, centerX: number, baseline: number, size: number, color: string): void {
    const width: number = sys.canvas.measureText(text, size);
    sys.canvas.setFillColor(color);
    sys.canvas.drawText(text, centerX - width * 0.5, baseline, size);
}

function drawButton(rect: Rect, label: string, primary: boolean, pointer: PointerState): boolean {
    const hovered: boolean = contains(rect, pointer.x, pointer.y);
    let background: string = COLORS.paper;
    let foreground: string = COLORS.ink;
    if (primary) {
        background = COLORS.ink;
        foreground = COLORS.paper;
    }
    if (hovered && pointer.down) background = COLORS.red;
    fillRect(rect, background);
    strokeRect(rect, COLORS.ink, density);
    const size: number = 13 * density;
    const metrics = sys.canvas.measureTextRect(label, size);
    drawCentered(label, rect.x + rect.width * 0.5, rect.y + rect.height * 0.5 + metrics.height * 0.5, size, foreground);
    return hovered;
}

function sliderRect(content: Rect): Rect {
    return {
        x: content.x,
        y: content.y + content.height - 92 * density,
        width: content.width,
        height: 54 * density
    };
}

function drawSlider(rect: Rect, pointer: PointerState, control: string, value: number, color: string, label: string): number {
    const trackY: number = rect.y + 21 * density;
    const knobX: number = rect.x + value * rect.width;
    drawRule(rect.x, trackY, rect.x + rect.width, COLORS.rule, 5 * density);
    drawRule(rect.x, trackY, knobX, color, 5 * density);
    sys.canvas.setFillColor(COLORS.paper);
    sys.canvas.drawCircle(knobX, trackY, 12 * density);
    sys.canvas.setStrokeColor(COLORS.ink);
    sys.canvas.setStrokeWidth(2 * density);
    sys.canvas.drawCircle(knobX, trackY, 12 * density);
    drawLabel('0', rect.x, rect.y + 50 * density, COLORS.muted);
    drawLabel('1', rect.x + rect.width - 7 * density, rect.y + 50 * density, COLORS.muted);
    drawCentered(label + format(value, 2), knobX, rect.y + 50 * density, 12 * density, COLORS.ink);

    if (pointer.pressed && contains(rect, pointer.x, pointer.y)) activeControl = control;
    if (activeControl === control && pointer.down) {
        value = clamp((pointer.x - rect.x) / rect.width, 0.01, 0.99);
    }
    return value;
}

function drawProbabilitySlider(rect: Rect, pointer: PointerState): void {
    probability = drawSlider(rect, pointer, 'probability', probability, COLORS.red, 'p = ');
}

function drawBitLesson(content: Rect): void {
    const bodySize: number = 17 * density;
    let y: number = content.y;
    y = drawParagraph('Information begins when more than one outcome is possible. A bit answers one perfectly balanced yes-or-no question.', content.x, y, content.width, bodySize, COLORS.muted, 25 * density);
    y += 20 * density;

    const gap: number = 12 * density;
    const boxWidth: number = (content.width - gap) * 0.5;
    const boxHeight: number = Math.min(150 * density, content.height - 190 * density);
    const zero: Rect = { x: content.x, y: y, width: boxWidth, height: boxHeight };
    const one: Rect = { x: content.x + boxWidth + gap, y: y, width: boxWidth, height: boxHeight };
    fillRect(zero, COLORS.teal);
    fillRect(one, COLORS.yellow);
    drawCentered('0', zero.x + zero.width * 0.5, zero.y + zero.height * 0.58, 52 * density, COLORS.ink);
    drawCentered('1', one.x + one.width * 0.5, one.y + one.height * 0.58, 52 * density, COLORS.ink);

    const noteY: number = y + boxHeight + 32 * density;
    drawParagraph('Two equiprobable outcomes need 1 bit. Four need 2 bits. Eight need 3. In general: log2(N) bits.', content.x, noteY, content.width, 15 * density, COLORS.ink, 22 * density);
}

function drawSurpriseLesson(content: Rect, pointer: PointerState): void {
    const bodySize: number = 17 * density;
    let y: number = content.y;
    y = drawParagraph('Shannon measures the information in an outcome by its surprise. An event expected half the time carries 1 bit; a one-in-eight event carries 3.', content.x, y, content.width, bodySize, COLORS.muted, 25 * density);
    y += 12 * density;

    const value: number = information(probability);
    const chartHeight: number = Math.min(150 * density, content.height - 225 * density);
    const chart: Rect = { x: content.x, y: y, width: content.width, height: chartHeight };
    fillRect(chart, COLORS.white);
    const maxBits: number = 7;
    const barWidth: number = Math.min(120 * density, chart.width * 0.26);
    const barHeight: number = clamp(value / maxBits, 0, 1) * (chart.height - 32 * density);
    const bar: Rect = { x: chart.x + 26 * density, y: chart.y + chart.height - barHeight - 16 * density, width: barWidth, height: barHeight };
    fillRect(bar, COLORS.red);
    sys.canvas.setFillColor(COLORS.ink);
    sys.canvas.drawText(format(value, 2) + ' bits', bar.x + bar.width + 20 * density, chart.y + chart.height * 0.53, 28 * density);
    sys.canvas.drawText('I(x) = -log2 p(x)', bar.x + bar.width + 20 * density, chart.y + chart.height * 0.53 + 29 * density, 15 * density);
    drawProbabilitySlider(sliderRect(content), pointer);
}

function drawEntropyCurve(rect: Rect): void {
    const baseline: number = rect.y + rect.height;
    const steps: number = 80;
    sys.canvas.setStrokeColor(COLORS.red);
    sys.canvas.setStrokeWidth(3 * density);
    let previousX: number = rect.x;
    let previousY: number = baseline;
    for (let index: number = 1; index <= steps; index += 1) {
        const p: number = index / steps;
        const x: number = rect.x + p * rect.width;
        const y: number = baseline - binaryEntropy(p) * rect.height;
        sys.canvas.drawLine(previousX, previousY, x, y);
        previousX = x;
        previousY = y;
    }
    drawRule(rect.x, baseline, rect.x + rect.width, COLORS.ink, density);
    const markerX: number = rect.x + probability * rect.width;
    const markerY: number = baseline - binaryEntropy(probability) * rect.height;
    sys.canvas.setFillColor(COLORS.yellow);
    sys.canvas.drawCircle(markerX, markerY, 8 * density);
    sys.canvas.setStrokeColor(COLORS.ink);
    sys.canvas.setStrokeWidth(2 * density);
    sys.canvas.drawCircle(markerX, markerY, 8 * density);
}

function drawEntropyLesson(content: Rect, pointer: PointerState): void {
    const bodySize: number = 17 * density;
    let y: number = content.y;
    y = drawParagraph('Entropy is the expected surprise before the event occurs. Weight each outcome by how often it happens, then add.', content.x, y, content.width, bodySize, COLORS.muted, 25 * density);
    y += 10 * density;

    const value: number = binaryEntropy(probability);
    sys.canvas.setFillColor(COLORS.ink);
    sys.canvas.drawText('H(X) = ' + format(value, 3) + ' bits', content.x, y + 28 * density, 28 * density);
    sys.canvas.setFillColor(COLORS.muted);
    sys.canvas.drawText('-p log2 p - (1-p) log2 (1-p)', content.x, y + 52 * density, 14 * density);

    const curve: Rect = {
        x: content.x,
        y: y + 78 * density,
        width: content.width,
        height: Math.min(112 * density, content.height - 250 * density)
    };
    drawEntropyCurve(curve);
    drawProbabilitySlider(sliderRect(content), pointer);
}

function drawDistributionBars(rect: Rect, values: number[], showInformation: boolean): void {
    const labels: string[] = ['A', 'B', 'C', 'D'];
    const gap: number = 12 * density;
    const barWidth: number = (rect.width - gap * 3) / 4;
    for (let index: number = 0; index < values.length; index += 1) {
        const height: number = Math.max(2 * density, values[index] * rect.height);
        const x: number = rect.x + index * (barWidth + gap);
        const bar: Rect = { x: x, y: rect.y + rect.height - height, width: barWidth, height: height };
        fillRect(bar, index === 0 ? COLORS.red : COLORS.teal);
        drawCentered(labels[index], x + barWidth * 0.5, rect.y + rect.height + 20 * density, 13 * density, COLORS.ink);
        drawCentered(format(values[index], 2), x + barWidth * 0.5, bar.y - 7 * density, 11 * density, COLORS.muted);
        if (showInformation && values[index] > 0) {
            drawCentered(format(information(values[index]), 2) + 'b', x + barWidth * 0.5, bar.y + Math.min(22 * density, height - 4 * density), 10 * density, COLORS.ink);
        }
    }
}

function drawAlphabetLesson(content: Rect, pointer: PointerState): void {
    let y: number = content.y;
    y = drawParagraph('For many symbols, entropy is the sum of each surprise weighted by its probability. Move mass toward A and watch uncertainty fall.', content.x, y, content.width, 17 * density, COLORS.muted, 25 * density);
    y += 10 * density;
    const values: number[] = distribution();
    const value: number = entropy(values);
    sys.canvas.setFillColor(COLORS.ink);
    sys.canvas.drawText('H(X) = ' + format(value, 3) + ' bits / symbol', content.x, y + 23 * density, 25 * density);
    drawLabel('MAXIMUM FOR FOUR SYMBOLS: log2(4) = 2 bits', content.x, y + 48 * density, COLORS.red);
    const bars: Rect = { x: content.x, y: y + 72 * density, width: content.width, height: Math.min(130 * density, content.height - 250 * density) };
    drawDistributionBars(bars, values, false);
    const shapeRect: Rect = sliderRect(content);
    distributionShape = drawSlider(shapeRect, pointer, 'shape', distributionShape, COLORS.teal, 'P(A) = ');
}

function empiricalProbability(): number {
    if (observations.length === 0) return 0;
    let ones: number = 0;
    for (let index: number = 0; index < observations.length; index += 1) ones += observations[index];
    return ones / observations.length;
}

function formatEstimate(value: number): string {
    if (observations.length === 0) return '-';
    return format(value, 3);
}

function drawSampleStrip(rect: Rect): void {
    const visible: number = Math.min(64, observations.length);
    if (visible === 0) {
        drawCentered('No observations yet', rect.x + rect.width * 0.5, rect.y + rect.height * 0.55, 15 * density, COLORS.muted);
        return;
    }
    const columns: number = 16;
    const gap: number = 3 * density;
    const cell: number = Math.min((rect.width - gap * (columns - 1)) / columns, 20 * density);
    const start: number = observations.length - visible;
    for (let index: number = 0; index < visible; index += 1) {
        const column: number = index % columns;
        const row: number = Math.floor(index / columns);
        const cellRect: Rect = {
            x: rect.x + column * (cell + gap),
            y: rect.y + row * (cell + gap),
            width: cell,
            height: cell
        };
        fillRect(cellRect, observations[start + index] === 1 ? COLORS.yellow : COLORS.teal);
    }
}

function drawSamplingLesson(content: Rect, pointer: PointerState): void {
    let y: number = content.y;
    y = drawParagraph('Entropy belongs to the source distribution, not to one finite sample. Draw observations and compare the empirical estimate with the true value.', content.x, y, content.width, 17 * density, COLORS.muted, 25 * density);
    y += 8 * density;
    const empirical: number = empiricalProbability();
    sys.canvas.setFillColor(COLORS.ink);
    sys.canvas.drawText('true H  ' + format(binaryEntropy(probability), 3), content.x, y + 24 * density, 21 * density);
    sys.canvas.drawText('sample H  ' + formatEstimate(binaryEntropy(empirical)), content.x + content.width * 0.52, y + 24 * density, 21 * density);
    drawLabel('N = ' + observations.length + '    observed p = ' + formatEstimate(empirical), content.x, y + 48 * density, COLORS.muted);
    const strip: Rect = { x: content.x, y: y + 68 * density, width: content.width, height: 92 * density };
    fillRect(strip, COLORS.white);
    drawSampleStrip({ x: strip.x + 10 * density, y: strip.y + 9 * density, width: strip.width - 20 * density, height: strip.height - 18 * density });

    const buttonWidth: number = Math.min(130 * density, content.width * 0.34);
    const oneRect: Rect = { x: content.x, y: strip.y + strip.height + 12 * density, width: buttonWidth, height: 32 * density };
    const batchRect: Rect = { x: oneRect.x + buttonWidth + 10 * density, y: oneRect.y, width: buttonWidth, height: 32 * density };
    drawButton(oneRect, 'DRAW 1', false, pointer);
    drawButton(batchRect, 'DRAW 32', true, pointer);
    if (pointerWasDown && !pointer.down) {
        if (contains(oneRect, pointer.x, pointer.y)) drawObservation();
        if (contains(batchRect, pointer.x, pointer.y)) drawObservationBatch(32);
    }
    drawProbabilitySlider(sliderRect(content), pointer);
}

function huffmanLengths(values: number[]): number[] {
    const nodes: HuffmanNode[] = [];
    const lengths: number[] = [0, 0, 0, 0];
    for (let index: number = 0; index < values.length; index += 1) nodes.push({ weight: values[index], members: [index] });
    while (nodes.length > 1) {
        nodes.sort(function (left, right) { return left.weight - right.weight; });
        const first = nodes.shift();
        const second = nodes.shift();
        if (!first || !second) break;
        const members: number[] = first.members.concat(second.members);
        for (let index: number = 0; index < members.length; index += 1) lengths[members[index]] += 1;
        nodes.push({ weight: first.weight + second.weight, members: members });
    }
    return lengths;
}

function drawCodingLesson(content: Rect, pointer: PointerState): void {
    let y: number = content.y;
    y = drawParagraph('A prefix code spends short words on common symbols. Shannon says no lossless code can beat H on average; Huffman stays below H + 1.', content.x, y, content.width, 17 * density, COLORS.muted, 25 * density);
    y += 8 * density;
    const values: number[] = distribution();
    const lengths: number[] = huffmanLengths(values);
    let expected: number = 0;
    for (let index: number = 0; index < values.length; index += 1) expected += values[index] * lengths[index];
    const symbols: string[] = ['A', 'B', 'C', 'D'];
    const rowHeight: number = 28 * density;
    drawLabel('SYMBOL      PROBABILITY      IDEAL -log2(p)      HUFFMAN LENGTH', content.x, y + 18 * density, COLORS.muted);
    for (let index: number = 0; index < values.length; index += 1) {
        const rowY: number = y + 49 * density + index * rowHeight;
        if (index % 2 === 0) fillRect({ x: content.x, y: rowY - 18 * density, width: content.width, height: rowHeight }, COLORS.white);
        sys.canvas.setFillColor(index === 0 ? COLORS.red : COLORS.ink);
        sys.canvas.drawText(symbols[index], content.x + 8 * density, rowY, 14 * density);
        sys.canvas.setFillColor(COLORS.ink);
        sys.canvas.drawText(format(values[index], 3), content.x + content.width * 0.25, rowY, 14 * density);
        sys.canvas.drawText(format(information(values[index]), 2), content.x + content.width * 0.55, rowY, 14 * density);
        sys.canvas.drawText(String(lengths[index]), content.x + content.width * 0.87, rowY, 14 * density);
    }
    const resultY: number = y + 49 * density + 4 * rowHeight + 16 * density;
    sys.canvas.setFillColor(COLORS.ink);
    sys.canvas.drawText('H = ' + format(entropy(values), 3) + '    Huffman average = ' + format(expected, 3) + ' bits', content.x, resultY, 20 * density);
    distributionShape = drawSlider(sliderRect(content), pointer, 'shape', distributionShape, COLORS.teal, 'P(A) = ');
}

function drawModelLesson(content: Rect, pointer: PointerState): void {
    let y: number = content.y;
    y = drawParagraph('Suppose the true source emits 1 with probability p, but your model assigns q. Coding with q costs cross-entropy; the excess is KL divergence.', content.x, y, content.width, 17 * density, COLORS.muted, 25 * density);
    y += 10 * density;
    const sourceEntropy: number = binaryEntropy(probability);
    const cost: number = crossEntropy(probability, modelProbability);
    const penalty: number = cost - sourceEntropy;
    const maxWidth: number = content.width;
    drawLabel('OPTIMAL SOURCE COST', content.x, y + 15 * density, COLORS.muted);
    fillRect({ x: content.x, y: y + 25 * density, width: maxWidth * clamp(sourceEntropy / 4, 0, 1), height: 26 * density }, COLORS.teal);
    sys.canvas.setFillColor(COLORS.ink);
    sys.canvas.drawText(format(sourceEntropy, 3) + ' bits', content.x + 8 * density, y + 44 * density, 13 * density);
    drawLabel('COST USING YOUR MODEL', content.x, y + 75 * density, COLORS.muted);
    fillRect({ x: content.x, y: y + 85 * density, width: maxWidth * clamp(cost / 4, 0, 1), height: 26 * density }, COLORS.yellow);
    sys.canvas.setFillColor(COLORS.ink);
    sys.canvas.drawText(format(cost, 3) + ' bits', content.x + 8 * density, y + 104 * density, 13 * density);
    sys.canvas.setFillColor(COLORS.red);
    sys.canvas.drawText('KL penalty = ' + format(penalty, 3) + ' bits / event', content.x, y + 142 * density, 22 * density);
    drawParagraph('The penalty reaches zero exactly when q = p. This is why minimizing log loss learns a probability model.', content.x, y + 169 * density, content.width, 14 * density, COLORS.muted, 20 * density);

    const firstSlider: Rect = { x: content.x, y: content.y + content.height - 142 * density, width: content.width, height: 54 * density };
    const secondSlider: Rect = sliderRect(content);
    probability = drawSlider(firstSlider, pointer, 'probability', probability, COLORS.teal, 'true p = ');
    modelProbability = drawSlider(secondSlider, pointer, 'model', modelProbability, COLORS.yellow, 'model q = ');
}

function layoutContent(width: number, height: number): Rect {
    const margin: number = 28 * density;
    if (width >= 820 * density) {
        return {
            x: Math.max(290 * density, width * 0.31),
            y: 142 * density,
            width: width - Math.max(290 * density, width * 0.31) - margin,
            height: height - 204 * density
        };
    }
    return {
        x: margin,
        y: 154 * density,
        width: width - margin * 2,
        height: height - 224 * density
    };
}

function drawChrome(width: number, height: number, pointer: PointerState): void {
    const desktop: boolean = width >= 820 * density;
    const margin: number = 28 * density;
    drawLabel('INFORMATION THEORY / AN INTERACTIVE PRIMER', margin, 34 * density, COLORS.muted);
    drawRule(margin, 50 * density, width - margin, COLORS.rule, density);

    if (desktop) {
        const railWidth: number = Math.max(250 * density, width * 0.26);
        sys.canvas.setFillColor(COLORS.ink);
        sys.canvas.drawText('SHANNON', margin, 92 * density, 34 * density);
        for (let index: number = 0; index < CHAPTERS.length; index += 1) {
            const y: number = 140 * density + index * 41 * density;
            if (index === chapter) fillRect({ x: margin - 8 * density, y: y - 25 * density, width: railWidth, height: 37 * density }, COLORS.yellow);
            sys.canvas.setFillColor(index === chapter ? COLORS.ink : COLORS.muted);
            sys.canvas.drawText('0' + (index + 1) + '  ' + CHAPTERS[index], margin, y, 14 * density);
        }
        drawVerticalRule(railWidth + margin, 72 * density, height - 72 * density, COLORS.rule, density);
    } else {
        sys.canvas.setFillColor(COLORS.ink);
        sys.canvas.drawText('SHANNON', margin, 88 * density, 28 * density);
        const segmentWidth: number = (width - margin * 2) / CHAPTERS.length;
        for (let index: number = 0; index < CHAPTERS.length; index += 1) {
            const x: number = margin + index * segmentWidth;
            drawRule(x, 116 * density, x + segmentWidth - 6 * density, index <= chapter ? COLORS.red : COLORS.rule, 4 * density);
        }
    }

    const content: Rect = layoutContent(width, height);
    drawLabel('0' + (chapter + 1) + ' / ' + CHAPTERS.length, content.x, 91 * density, COLORS.red);
    sys.canvas.setFillColor(COLORS.ink);
    sys.canvas.drawText(TITLES[chapter], content.x, 126 * density, 29 * density);

    const navY: number = height - 50 * density;
    const buttonWidth: number = 96 * density;
    const previousRect: Rect = { x: content.x, y: navY, width: buttonWidth, height: 34 * density };
    const nextRect: Rect = { x: content.x + content.width - buttonWidth, y: navY, width: buttonWidth, height: 34 * density };
    if (chapter > 0) drawButton(previousRect, '<  PREV', false, pointer);
    if (chapter < CHAPTERS.length - 1) drawButton(nextRect, 'NEXT  >', true, pointer);

    if (pointerWasDown && !pointer.down) {
        if (chapter > 0 && contains(previousRect, pointer.x, pointer.y)) chapter -= 1;
        if (chapter < CHAPTERS.length - 1 && contains(nextRect, pointer.x, pointer.y)) chapter += 1;
    }
}

function handleKeyboard(): void {
    if (sys.input.isKeyPressed(80) && chapter > 0) chapter -= 1;
    if (sys.input.isKeyPressed(79) && chapter < CHAPTERS.length - 1) chapter += 1;
}

function frame(timestamp: number): void {
    const width: number = sys.window.getWidth();
    const height: number = sys.window.getHeight();
    density = sys.window.getDisplayDensity();
    const input = sys.input.get();
    const pointer: PointerState = input.pointer;

    handleKeyboard();
    sys.canvas.clear(COLORS.paper);
    drawChrome(width, height, pointer);
    const content: Rect = layoutContent(width, height);
    if (chapter === 0) drawBitLesson(content);
    if (chapter === 1) drawSurpriseLesson(content, pointer);
    if (chapter === 2) drawEntropyLesson(content, pointer);
    if (chapter === 3) drawAlphabetLesson(content, pointer);
    if (chapter === 4) drawSamplingLesson(content, pointer);
    if (chapter === 5) drawCodingLesson(content, pointer);
    if (chapter === 6) drawModelLesson(content, pointer);

    if (!pointer.down) activeControl = '';
    pointerWasDown = pointer.down;
    sys.animation.requestFrame(frame);
}

sys.animation.requestFrame(frame);