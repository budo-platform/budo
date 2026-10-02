/**
 * Tetris — Budo
 *
 * Classic Tetris with:
 *  - Standard 7 tetrominoes, SRS-ish rotations (kick-free)
 *  - Soft drop, hard drop, hold, next preview
 *  - Line clears with score/level/gravity scaling
 *  - Subtle but striking post-processing (bloom + CRT) via shader.frag
 *
 * Controls:
 *   ← / →  move
 *   ↓      soft drop
 *   ↑ / X  rotate clockwise
 *   Z      rotate counter-clockwise
 *   Space  hard drop
 *   C      hold
 *   P      pause
 *   R      restart (after game over)
 *
 * Touch:
 *   Drag on board        move left/right
 *   Tap board            rotate clockwise
 *   Hold ↓ button        soft drop
 *   Tap DROP             hard drop
 *   Tap HOLD / CCW / CW  hold or rotate
 *   Tap II               pause/resume
 */

// GL program wrapper

class ShaderProgram {
    programId

    constructor(programId) {
        this.programId = programId
        if (this.programId < 0) throw "invalid program id"
    }

    uniform1i(name, value) {
        sys.gl.setUniform1i(this.programId, name, value)
        return this
    }

    uniform1f(name, value) {
        sys.gl.setUniform1f(this.programId, name, value)
        return this
    }

    uniform2f(name, value0, value1) {
        sys.gl.setUniform2f(this.programId, name, value0, value1)
        return this
    }

    uniform3f(name, v0, v1, v2) {
        sys.gl.setUniform3f(this.programId, name, v0, v1, v2)
        return this
    }

    uniform4f(name, v0, v1, v2, v3) {
        sys.gl.setUniform4f(this.programId, name, v0, v1, v2, v3)
        return this
    }

    uniformMatrix4(name, mat16) {
        sys.gl.setUniformMatrix4(this.programId, name, mat16)
        return this
    }

    // in the original c wrapper program.drawFullscreen was calling gl.drawFullscreenImmediate
    drawFullscreen() {
        sys.gl.drawFullscreenImmediate(this.programId)
        return this
    }

    drawRegion(x, y, w, h, targetId) {
        sys.gl.drawRegionImmediate(this.programId, x, y, w, h, targetId)
        return this
    }

    // in the original c wrapper program.drawMesh was calling gl.drawMesgImmediate
    drawMesh(layoutId, options) {
        sys.gl.drawMeshImmediate(this.programId, layoutId, options)
    }
}

// --- Post-processing ---------------------------------------------------------

const shaderProgram = new ShaderProgram(sys.gl.createProgram('shader.vert', 'shader.frag'));
let flash = 0;

// --- Board geometry ----------------------------------------------------------

const COLS = 10;
const ROWS = 20;

// --- SDL scancodes -----------------------------------------------------------

const KEY = {
    LEFT: 80,
    RIGHT: 79,
    DOWN: 81,
    UP: 82,
    SPACE: 44,
    Z: 29,
    X: 27,
    C: 6,
    P: 19,
    R: 21,
};

// --- Tetromino definitions ---------------------------------------------------
// Each piece is described by a list of 4-rotation states. Each state is a
// 4-element array of [x, y] offsets from the piece origin.

function rot(cells) {
    // Rotate cells 90° clockwise around (1.5, 1.5) for 4x4-ish bounding boxes.
    return cells.map(([x, y]) => [3 - y, x]);
}

function buildRotations(base) {
    const states = [base];
    for (let i = 0; i < 3; i++) states.push(rot(states[states.length - 1]));
    return states;
}

const PIECES = {
    I: { color: '#4ECDC4', states: buildRotations([[0, 1], [1, 1], [2, 1], [3, 1]]) },
    O: { color: '#FFD93D', states: [[[1, 0], [2, 0], [1, 1], [2, 1]]] },
    T: { color: '#C792EA', states: buildRotations([[1, 0], [0, 1], [1, 1], [2, 1]]) },
    S: { color: '#7BE495', states: buildRotations([[1, 0], [2, 0], [0, 1], [1, 1]]) },
    Z: { color: '#FF6B6B', states: buildRotations([[0, 0], [1, 0], [1, 1], [2, 1]]) },
    J: { color: '#5DA9E9', states: buildRotations([[0, 0], [0, 1], [1, 1], [2, 1]]) },
    L: { color: '#FFA15C', states: buildRotations([[2, 0], [0, 1], [1, 1], [2, 1]]) },
};
const PIECE_NAMES = Object.keys(PIECES);

// --- Game state --------------------------------------------------------------

let board = createBoard();
let bag = [];
let current = null;
let next = null;
let hold = null;
let canHold = true;

let score = 0;
let lines = 0;
let level = 1;
let gameOver = false;
let paused = false;

let dropTimer = 0;
let softDrop = false;
let wasSoftDrop = false;
let smoothedFPS = 0;

// Auto-repeat for left/right
const DAS = 0.16;   // delayed auto-shift
const ARR = 0.045;  // auto-repeat rate
let moveDir = 0;
let moveTimer = 0;
let moveInitial = false;

function pressed(scancode) {
    return sys.input.isKeyPressed(scancode);
}

// Touch controls and board gestures.
const touchDrags = new Map();
const touchActiveButtons = new Set();
const touchActions = [];
let touchPreviousPointerIds = new Set();
let touchMoveDir = 0;
let touchSoftDrop = false;

// --- Helpers -----------------------------------------------------------------

function createBoard() {
    const b = new Array(ROWS);
    for (let y = 0; y < ROWS; y++) {
        b[y] = new Array(COLS).fill(null);
    }
    return b;
}

function refillBag() {
    const arr = PIECE_NAMES.slice();
    for (let i = arr.length - 1; i > 0; i--) {
        const j = Math.floor(Math.random() * (i + 1));
        [arr[i], arr[j]] = [arr[j], arr[i]];
    }
    bag.push(...arr);
}

function nextFromBag() {
    if (bag.length === 0) refillBag();
    return bag.shift();
}

function spawnPiece(name) {
    const piece = PIECES[name];
    return {
        name,
        color: piece.color,
        rotation: 0,
        x: 3,
        y: -1,
        states: piece.states,
    };
}

function cellsOf(piece) {
    return piece.states[piece.rotation % piece.states.length];
}

function collides(piece, dx, dy, drot) {
    const r = (piece.rotation + (drot || 0) + 4) % piece.states.length;
    const cells = piece.states[r];
    for (let i = 0; i < cells.length; i++) {
        const cx = piece.x + cells[i][0] + dx;
        const cy = piece.y + cells[i][1] + dy;
        if (cx < 0 || cx >= COLS || cy >= ROWS) return true;
        if (cy >= 0 && board[cy][cx]) return true;
    }
    return false;
}

function lockPiece() {
    const cells = cellsOf(current);
    for (let i = 0; i < cells.length; i++) {
        const cx = current.x + cells[i][0];
        const cy = current.y + cells[i][1];
        if (cy < 0) {
            gameOver = true;
            return;
        }
        board[cy][cx] = current.color;
    }

    // Clear lines.
    let cleared = 0;
    for (let y = ROWS - 1; y >= 0; y--) {
        let full = true;
        for (let x = 0; x < COLS; x++) {
            if (!board[y][x]) { full = false; break; }
        }
        if (full) {
            board.splice(y, 1);
            board.unshift(new Array(COLS).fill(null));
            cleared++;
            y++; // recheck same index
        }
    }

    if (cleared > 0) {
        const points = [0, 100, 300, 500, 800][cleared] * level;
        score += points;
        lines += cleared;
        const newLevel = Math.floor(lines / 10) + 1;
        if (newLevel !== level) level = newLevel;
        flash = Math.min(1.0, 0.4 + cleared * 0.2);
    }

    spawnNext();
}

function spawnNext() {
    current = spawnPiece(next);
    next = nextFromBag();
    canHold = true;

    if (collides(current, 0, 0, 0)) {
        gameOver = true;
    }
}

function rotateCurrent(dir) {
    if (collides(current, 0, 0, dir)) {
        // Tiny wall kicks.
        for (const dx of [-1, 1, -2, 2]) {
            if (!collides(current, dx, 0, dir)) {
                current.x += dx;
                current.rotation = (current.rotation + dir + 4) % current.states.length;
                return;
            }
        }
        return;
    }
    current.rotation = (current.rotation + dir + 4) % current.states.length;
}

function move(dx) {
    if (!collides(current, dx, 0, 0)) {
        current.x += dx;
    }
}

function stepDown() {
    if (collides(current, 0, 1, 0)) {
        lockPiece();
        return false;
    }
    current.y++;
    return true;
}

function hardDrop() {
    let dist = 0;
    while (!collides(current, 0, 1, 0)) {
        current.y++;
        dist++;
    }
    score += dist * 2;
    lockPiece();
}

function holdPiece() {
    if (!canHold) return;
    if (hold === null) {
        hold = current.name;
        spawnNext();
    } else {
        const tmp = hold;
        hold = current.name;
        current = spawnPiece(tmp);
    }
    canHold = false;
}

function gravityInterval() {
    // Classic NES-ish curve, capped.
    const sec = Math.max(0.05, Math.pow(0.8 - (level - 1) * 0.007, level - 1));
    return sec;
}

function resetGame() {
    board = createBoard();
    bag = [];
    score = 0;
    lines = 0;
    level = 1;
    hold = null;
    canHold = true;
    gameOver = false;
    paused = false;
    dropTimer = 0;
    wasSoftDrop = false;
    flash = 0;
    next = nextFromBag();
    spawnNext();
}

// --- Rendering ---------------------------------------------------------------

const BG = '#0A0A14';
const PANEL = '#15152A';
const GRID = '#1F1F38';
const FRAME = '#3A3A6A';
const TEXT = '#E6E6F0';
const TEXT_DIM = '#9090B0';
const GHOST_ALPHA = 60;

function computeLayout() {
    const W = sys.window.getWidth();
    const H = sys.window.getHeight();

    const isTouchLayout = W < 700 || H > W * 1.15;
    if (isTouchLayout) {
        const padding = Math.max(10, Math.floor(Math.min(W, H) * 0.035));
        const topHudH = Math.max(64, Math.min(88, Math.floor(H * 0.12)));
        const controlsH = Math.max(132, Math.min(180, Math.floor(H * 0.24)));
        const availableH = H - topHudH - controlsH - padding * 2;
        const cellByH = Math.floor(availableH / ROWS);
        const cellByW = Math.floor((W - padding * 2) / COLS);
        const cell = Math.max(10, Math.min(cellByH, cellByW));
        const boardW = cell * COLS;
        const boardH = cell * ROWS;
        const boardX = Math.floor((W - boardW) / 2);
        const playAreaTop = topHudH + padding;
        const playAreaBottom = H - controlsH - padding;
        const boardY = Math.floor(playAreaTop + (playAreaBottom - playAreaTop - boardH) / 2);
        return { W, H, cell, boardW, boardH, boardX, boardY, isTouchLayout, topHudH, controlsH, padding };
    }

    // Fit a 10x20 board with side panels.
    // Reserve roughly 60% width for the board, 40% for side panels.
    const sideMin = 140;
    const padding = 16;
    const cellByH = Math.floor((H - padding * 2) / ROWS);
    const cellByW = Math.floor((W - padding * 2 - sideMin * 2) / COLS);
    const cell = Math.max(12, Math.min(cellByH, cellByW));
    const boardW = cell * COLS;
    const boardH = cell * ROWS;
    const boardX = Math.floor((W - boardW) / 2);
    const boardY = Math.floor((H - boardH) / 2);
    return { W, H, cell, boardW, boardH, boardX, boardY, isTouchLayout, topHudH: 0, controlsH: 0, padding };
}

function drawCell(x, y, size, color, alpha) {
    if (alpha !== undefined) sys.canvas.setAlpha(alpha);
    sys.canvas.setFillColor(color);
    sys.canvas.drawRect(x + 1, y + 1, size - 2, size - 2);
    // Highlight
    sys.canvas.setFillColor('#FFFFFF');
    sys.canvas.setAlpha((alpha !== undefined ? alpha : 255) * 0.18);
    sys.canvas.drawRect(x + 1, y + 1, size - 2, Math.max(1, Math.floor(size * 0.18)));
    sys.canvas.setAlpha(255);
}

function drawBoard(layout) {
    const { boardX, boardY, boardW, boardH, cell } = layout;

    // Backdrop
    sys.canvas.setFillColor(PANEL);
    sys.canvas.drawRect(boardX - 4, boardY - 4, boardW + 8, boardH + 8);

    // Grid lines
    sys.canvas.setStrokeColor(GRID);
    sys.canvas.setStrokeWidth(1);
    for (let x = 0; x <= COLS; x++) {
        sys.canvas.drawLine(boardX + x * cell, boardY, boardX + x * cell, boardY + boardH);
    }
    for (let y = 0; y <= ROWS; y++) {
        sys.canvas.drawLine(boardX, boardY + y * cell, boardX + boardW, boardY + y * cell);
    }

    // Locked cells
    for (let y = 0; y < ROWS; y++) {
        for (let x = 0; x < COLS; x++) {
            const c = board[y][x];
            if (c) drawCell(boardX + x * cell, boardY + y * cell, cell, c);
        }
    }

    // Ghost piece (gray shadow at landing position)
    if (current && !gameOver) {
        let dy = 0;
        while (!collides(current, 0, dy + 1, 0)) dy++;
        const cells = cellsOf(current);
        for (let i = 0; i < cells.length; i++) {
            const cx = current.x + cells[i][0];
            const cy = current.y + cells[i][1] + dy;
            if (cy >= 0) drawCell(boardX + cx * cell, boardY + cy * cell, cell, '#6A6A7A', GHOST_ALPHA);
        }
    }

    // Active piece
    if (current && !gameOver) {
        const cells = cellsOf(current);
        for (let i = 0; i < cells.length; i++) {
            const cx = current.x + cells[i][0];
            const cy = current.y + cells[i][1];
            if (cy >= 0) drawCell(boardX + cx * cell, boardY + cy * cell, cell, current.color);
        }
    }

    // Frame
    sys.canvas.setStrokeColor(FRAME);
    sys.canvas.setStrokeWidth(2);
    sys.canvas.drawRect(boardX, boardY, boardW, boardH);
}

function drawMiniPiece(name, x, y, cell) {
    if (!name) return;
    const cells = PIECES[name].states[0];
    let minX = 4, minY = 4, maxX = -1, maxY = -1;
    for (let i = 0; i < cells.length; i++) {
        if (cells[i][0] < minX) minX = cells[i][0];
        if (cells[i][1] < minY) minY = cells[i][1];
        if (cells[i][0] > maxX) maxX = cells[i][0];
        if (cells[i][1] > maxY) maxY = cells[i][1];
    }
    const w = (maxX - minX + 1) * cell;
    const h = (maxY - minY + 1) * cell;
    const ox = x - w / 2 - minX * cell;
    const oy = y - h / 2 - minY * cell;
    for (let i = 0; i < cells.length; i++) {
        drawCell(ox + cells[i][0] * cell, oy + cells[i][1] * cell, cell, PIECES[name].color);
    }
}

function drawPanel(label, x, y, w, h, pieceName) {
    sys.canvas.setFillColor(PANEL);
    sys.canvas.drawRect(x, y, w, h);
    sys.canvas.setStrokeColor(FRAME);
    sys.canvas.setStrokeWidth(2);
    sys.canvas.drawRect(x, y, w, h);

    sys.canvas.setFillColor(TEXT_DIM);
    sys.canvas.drawText(label, x + 12, y + 22, 14);

    drawMiniPiece(pieceName, x + w / 2, y + h / 2 + 8, Math.max(10, Math.floor(w / 6)));
}

function drawCenteredText(text, x, y, fontSize) {
    const textW = sys.canvas.measureText(text, fontSize);
    sys.canvas.drawText(text, x - textW / 2, y, fontSize);
}

function drawCompactHud(layout) {
    const { W, topHudH, padding } = layout;
    const hudX = padding;
    const hudY = padding;
    const hudW = W - padding * 2;
    const hudH = topHudH - padding;

    sys.canvas.setFillColor(PANEL);
    sys.canvas.drawRect(hudX, hudY, hudW, hudH);
    sys.canvas.setStrokeColor(FRAME);
    sys.canvas.setStrokeWidth(2);
    sys.canvas.drawRect(hudX, hudY, hudW, hudH);

    const miniCell = Math.max(7, Math.floor(hudH / 5));
    drawMiniPiece(hold, hudX + 30, hudY + hudH / 2 + 3, miniCell);
    drawMiniPiece(next, hudX + hudW - 30, hudY + hudH / 2 + 3, miniCell);

    sys.canvas.setFillColor(TEXT_DIM);
    sys.canvas.drawText('HOLD', hudX + 8, hudY + 18, 10);
    const nextW = sys.canvas.measureText('NEXT', 10);
    sys.canvas.drawText('NEXT', hudX + hudW - nextW - 8, hudY + 18, 10);

    const statY = hudY + Math.floor(hudH * 0.48);
    const valueY = hudY + Math.floor(hudH * 0.78);
    const statXs = [hudX + hudW * 0.33, hudX + hudW * 0.50, hudX + hudW * 0.67];
    const labels = ['SCORE', 'LINES', 'LEVEL'];
    const values = [String(score), String(lines), String(level)];
    for (let i = 0; i < labels.length; i++) {
        sys.canvas.setFillColor(TEXT_DIM);
        drawCenteredText(labels[i], statXs[i], statY, 10);
        sys.canvas.setFillColor(TEXT);
        drawCenteredText(values[i], statXs[i], valueY, 15);
    }
}

function buildTouchButtons(layout) {
    const { W, H, padding, controlsH } = layout;
    const gap = Math.max(8, Math.floor(W * 0.025));
    const sizeByWidth = Math.floor((W - padding * 2 - gap * 4) / 5);
    const sizeByHeight = Math.floor((controlsH - gap * 2) / 2);
    const size = Math.max(42, Math.min(62, sizeByWidth, sizeByHeight));
    const radius = size / 2;
    const bottomY = H - padding - radius;
    const topY = bottomY - size - gap;
    const leftX = padding + radius;
    const rightX = leftX + size + gap;
    const middleX = leftX + (size + gap) / 2;
    const actionRightX = W - padding - radius;
    const actionLeftX = actionRightX - size - gap;

    return [
        { action: 'left', label: '<', x: leftX, y: bottomY, r: radius, repeat: true },
        { action: 'right', label: '>', x: rightX, y: bottomY, r: radius, repeat: true },
        { action: 'soft', label: 'v', x: middleX, y: topY, r: radius, repeat: true },
        { action: 'ccw', label: 'CCW', x: actionLeftX, y: topY, r: radius },
        { action: 'cw', label: 'CW', x: actionLeftX, y: bottomY, r: radius },
        { action: 'hold', label: 'HOLD', x: actionRightX, y: topY, r: radius },
        { action: 'drop', label: 'DROP', x: actionRightX, y: bottomY, r: radius },
        { action: 'pause', label: 'II', x: W - padding - 22, y: padding + 22, r: 22 },
    ];
}

function hitCircle(px, py, button) {
    const dx = px - button.x;
    const dy = py - button.y;
    return dx * dx + dy * dy <= button.r * button.r;
}

function pointInBoard(px, py, layout) {
    return px >= layout.boardX && px <= layout.boardX + layout.boardW
        && py >= layout.boardY && py <= layout.boardY + layout.boardH;
}

function getTouchPointers(input) {
    if (Array.isArray(input.pointers) && input.pointers.length > 0) return input.pointers;
    if (input.pointer && input.pointer.down) return [input.pointer];
    if (input.mouse && input.mouse.left) {
        return [{ id: 'mouse', x: input.mouse.x, y: input.mouse.y, pressed: input.mouse.leftPressed }];
    }
    return [];
}

function queueTouchAction(action) {
    touchActions.push(action);
}

function updateTouchInput(input, layout) {
    touchActiveButtons.clear();
    touchMoveDir = 0;
    touchSoftDrop = false;
    if (!layout.isTouchLayout) {
        touchPreviousPointerIds = new Set();
        touchDrags.clear();
        return;
    }

    const buttons = buildTouchButtons(layout);
    const pointers = getTouchPointers(input);
    const activeIds = new Set();

    for (const pointer of pointers) {
        const pointerId = pointer.id !== undefined ? pointer.id : 'pointer';
        activeIds.add(pointerId);
        const justPressed = !!pointer.pressed || !touchPreviousPointerIds.has(pointerId);
        let consumed = false;

        for (const button of buttons) {
            if (hitCircle(pointer.x, pointer.y, button)) {
                touchActiveButtons.add(button.action);
                consumed = true;
                if (button.action === 'left') touchMoveDir = Math.min(touchMoveDir, -1);
                if (button.action === 'right') touchMoveDir = Math.max(touchMoveDir, 1);
                if (button.action === 'soft') touchSoftDrop = true;
                if (!button.repeat && justPressed) queueTouchAction(button.action);
            }
        }

        if (consumed) {
            touchDrags.delete(pointerId);
            continue;
        }

        if (justPressed && pointInBoard(pointer.x, pointer.y, layout)) {
            touchDrags.set(pointerId, {
                startX: pointer.x,
                startY: pointer.y,
                lastX: pointer.x,
                moved: false,
            });
        }

        const drag = touchDrags.get(pointerId);
        if (drag) {
            const step = Math.max(18, layout.cell * 0.72);
            let dx = pointer.x - drag.lastX;
            while (Math.abs(dx) >= step) {
                const dir = dx > 0 ? 1 : -1;
                if (!paused && !gameOver) move(dir);
                drag.lastX += dir * step;
                dx = pointer.x - drag.lastX;
                drag.moved = true;
            }

            const totalDx = pointer.x - drag.startX;
            const totalDy = pointer.y - drag.startY;
            if (Math.abs(totalDx) > layout.cell || Math.abs(totalDy) > layout.cell) {
                drag.moved = true;
            }
            if (totalDy > layout.cell * 1.2 && Math.abs(totalDx) < layout.cell * 1.5) {
                if (!paused && !gameOver) touchSoftDrop = true;
                drag.moved = true;
            }
        }
    }

    for (const [pointerId, drag] of touchDrags.entries()) {
        if (!activeIds.has(pointerId)) {
            if (!drag.moved) queueTouchAction('cw');
            touchDrags.delete(pointerId);
        }
    }

    touchPreviousPointerIds = activeIds;
}

function drawTouchControls(layout) {
    if (!layout.isTouchLayout) return;
    const buttons = buildTouchButtons(layout);

    for (const button of buttons) {
        const active = touchActiveButtons.has(button.action);
        sys.canvas.setFillColor(active ? '#4ECDC490' : '#FFFFFF30');
        sys.canvas.drawCircle(button.x, button.y, button.r);
        sys.canvas.setStrokeColor(active ? '#E6E6F0E0' : '#FFFFFF70');
        sys.canvas.setStrokeWidth(2);
        sys.canvas.drawCircle(button.x, button.y, button.r);
        sys.canvas.setFillColor(TEXT);
        drawCenteredText(button.label, button.x, button.y + (button.label.length > 2 ? 5 : 7), button.label.length > 2 ? 12 : 20);
    }

    sys.canvas.setFillColor('#FFFFFF70');
    drawCenteredText('drag board to move, tap board to rotate', layout.W / 2, layout.H - layout.padding - layout.controlsH + 20, 11);
}

function drawHud(layout) {
    if (layout.isTouchLayout) {
        drawCompactHud(layout);
        return;
    }

    const { boardX, boardY, boardW, boardH, W, H } = layout;
    const rightX = boardX + boardW + 16;
    const leftX = boardX - 16;

    const panelW = Math.min(160, W - (boardX + boardW) - 24);
    if (panelW < 80) return; // not enough room
    const panelH = 110;

    // Right side: NEXT, then stats.
    drawPanel('NEXT', rightX, boardY, panelW, panelH, next);

    const statsY = boardY + panelH + 12;
    sys.canvas.setFillColor(PANEL);
    sys.canvas.drawRect(rightX, statsY, panelW, 170);
    sys.canvas.setStrokeColor(FRAME);
    sys.canvas.setStrokeWidth(2);
    sys.canvas.drawRect(rightX, statsY, panelW, 170);

    sys.canvas.setFillColor(TEXT_DIM);
    sys.canvas.drawText('SCORE', rightX + 12, statsY + 24, 14);
    sys.canvas.setFillColor(TEXT);
    sys.canvas.drawText(String(score), rightX + 12, statsY + 50, 22);

    sys.canvas.setFillColor(TEXT_DIM);
    sys.canvas.drawText('LINES', rightX + 12, statsY + 84, 14);
    sys.canvas.setFillColor(TEXT);
    sys.canvas.drawText(String(lines), rightX + 12, statsY + 108, 22);

    sys.canvas.setFillColor(TEXT_DIM);
    sys.canvas.drawText('LEVEL', rightX + 12, statsY + 138, 14);
    sys.canvas.setFillColor(TEXT);
    sys.canvas.drawText(String(level), rightX + 12, statsY + 162, 22);

    // Left side: HOLD + controls
    const holdX = leftX - panelW;
    if (holdX > 8) {
        drawPanel('HOLD', holdX, boardY, panelW, panelH, hold);

        const ctrlY = boardY + panelH + 12;
        sys.canvas.setFillColor(PANEL);
        sys.canvas.drawRect(holdX, ctrlY, panelW, 170);
        sys.canvas.setStrokeColor(FRAME);
        sys.canvas.setStrokeWidth(2);
        sys.canvas.drawRect(holdX, ctrlY, panelW, 170);

        sys.canvas.setFillColor(TEXT_DIM);
        sys.canvas.drawText('CONTROLS', holdX + 12, ctrlY + 22, 14);

        sys.canvas.setFillColor(TEXT);
        const lh = 18;
        let ly = ctrlY + 44;
        sys.canvas.drawText('\u2190 \u2192  move', holdX + 12, ly, 12); ly += lh;
        sys.canvas.drawText('\u2193    soft', holdX + 12, ly, 12); ly += lh;
        sys.canvas.drawText('\u2191/X  rotate', holdX + 12, ly, 12); ly += lh;
        sys.canvas.drawText('Z    rot ccw', holdX + 12, ly, 12); ly += lh;
        sys.canvas.drawText('SPACE drop', holdX + 12, ly, 12); ly += lh;
        sys.canvas.drawText('C    hold', holdX + 12, ly, 12); ly += lh;
        sys.canvas.drawText('P    pause', holdX + 12, ly, 12);
    }
}

function drawFPS(layout) {
    const text = `FPS: ${smoothedFPS.toFixed(0)}`;
    const fontSize = layout.isTouchLayout ? 12 : 14;
    const textWidth = sys.canvas.measureText(text, fontSize);
    const x = layout.isTouchLayout ? (layout.W - textWidth) / 2 : 10;
    const y = layout.isTouchLayout ? layout.padding + 18 : 24;

    sys.canvas.setFillColor('#000000A0');
    sys.canvas.drawRoundRect(x - 5, y - fontSize - 3,
        textWidth + 10, fontSize + 8, 5, 5);
    sys.canvas.setFillColor(smoothedFPS >= 50 ? '#7CFF8B'
        : smoothedFPS >= 30 ? '#FFE066'
            : '#FF6666');
    sys.canvas.drawText(text, x, y, fontSize);
}

function drawOverlay(layout, title, subtitle) {
    const { W, H } = layout;
    sys.canvas.setFillColor('#000000');
    sys.canvas.setAlpha(170);
    sys.canvas.drawRect(0, 0, W, H);
    sys.canvas.setAlpha(255);

    sys.canvas.setFillColor(TEXT);
    const tw = sys.canvas.measureText(title, 48);
    sys.canvas.drawText(title, (W - tw) / 2, H / 2 - 10, 48);

    if (subtitle) {
        sys.canvas.setFillColor(TEXT_DIM);
        const sw = sys.canvas.measureText(subtitle, 18);
        sys.canvas.drawText(subtitle, (W - sw) / 2, H / 2 + 24, 18);
    }
}

// --- Main loop ---------------------------------------------------------------

function handleTouchAction(action) {
    if (action === 'pause') {
        if (!gameOver) paused = !paused;
        return;
    }
    if (action === 'restart') {
        resetGame();
        return;
    }
    if (gameOver || paused) return;

    if (action === 'cw') rotateCurrent(1);
    else if (action === 'ccw') rotateCurrent(-1);
    else if (action === 'drop') hardDrop();
    else if (action === 'hold') holdPiece();
}

function handleInput(dt, input, layout) {
    updateTouchInput(input, layout);

    if (gameOver) {
        if (pressed(KEY.R)) resetGame();
        while (touchActions.length > 0) {
            const action = touchActions.shift();
            if (action === 'pause' || action === 'drop' || action === 'cw') resetGame();
            else handleTouchAction(action);
        }
        return;
    }

    if (pressed(KEY.P)) paused = !paused;
    while (touchActions.length > 0) handleTouchAction(touchActions.shift());
    if (paused) return;

    // Horizontal movement with DAS/ARR.
    const left = sys.input.isKeyDown(KEY.LEFT);
    const right = sys.input.isKeyDown(KEY.RIGHT);
    const dir = touchMoveDir !== 0 ? touchMoveDir : (right ? 1 : 0) - (left ? 1 : 0);

    if (dir !== moveDir) {
        moveDir = dir;
        moveTimer = 0;
        moveInitial = false;
        if (dir !== 0) move(dir);
    } else if (dir !== 0) {
        moveTimer += dt;
        const threshold = moveInitial ? ARR : DAS;
        if (moveTimer >= threshold) {
            moveTimer = 0;
            moveInitial = true;
            move(dir);
        }
    }

    if (pressed(KEY.UP) || pressed(KEY.X)) rotateCurrent(1);
    if (pressed(KEY.Z)) rotateCurrent(-1);
    if (pressed(KEY.SPACE)) hardDrop();
    if (pressed(KEY.C)) holdPiece();

    softDrop = touchSoftDrop || sys.input.isKeyDown(KEY.DOWN);
}

function frame(timestamp) {
    const layout = computeLayout();
    const input = sys.input.get();
    const rawDt = input.deltaTime;
    const dt = Math.min(rawDt, 0.05);

    if (rawDt > 0) {
        const fps = 1 / rawDt;
        smoothedFPS = smoothedFPS === 0
            ? fps
            : smoothedFPS + (fps - smoothedFPS) * 0.12;
    }

    handleInput(dt, input, layout);

    if (!paused && !gameOver) {
        const interval = softDrop ? Math.min(0.05, gravityInterval()) : gravityInterval();
        if (softDrop && !wasSoftDrop) dropTimer = 0;
        dropTimer += dt;
        while (dropTimer >= interval) {
            dropTimer -= interval;
            if (!stepDown() && softDrop) {
                // landing while soft-dropping; reset timer
                dropTimer = 0;
                break;
            }
            if (softDrop) score += 1;
        }
    }
    wasSoftDrop = softDrop;

    // Decay flash for shader.
    flash = Math.max(0, flash - dt * 1.6);

    // --- Draw ---
    sys.canvas.clear(BG);

    // Subtle vertical gradient stripes for depth.
    sys.canvas.setFillColor('#10101F');
    sys.canvas.drawRect(0, 0, layout.W, layout.H * 0.5);

    drawBoard(layout);
    drawHud(layout);
    drawFPS(layout);

    if (paused) drawOverlay(layout, 'PAUSED', layout.isTouchLayout ? 'Tap II to resume' : 'Press P to resume');
    if (gameOver) drawOverlay(layout, 'GAME OVER', layout.isTouchLayout ? 'Tap board or DROP to play again' : 'Press R to play again');

    drawTouchControls(layout);

    // Apply post-process shader.
    shaderProgram.uniform1f('u_flash', flash);
    sys.gl.bindScreen();
    shaderProgram.drawFullscreen();

    sys.animation.requestFrame(frame);
}

// --- Bootstrap ---------------------------------------------------------------

resetGame();
sys.animation.requestFrame(frame);
