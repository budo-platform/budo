// ============================================
// Color palette
// ============================================

const COLORS = {
    bg: '#F5F5F0',
    header: '#2C3E50',
    headerText: '#FFFFFF',
    card: '#FFFFFF',
    cardDone: '#E8F5E9',
    text: '#333333',
    textDone: '#81C784',
    textMuted: '#999999',
    accent: '#3498DB',
    accentHover: '#2980B9',
    danger: '#E74C3C',
    dangerHover: '#C0392B',
    success: '#27AE60',
    inputBg: '#FFFFFF',
    inputBorder: '#BDC3C7',
    shadow: '#00000020',
};

// ============================================
// Layout constants
// ============================================

const HEADER_HEIGHT = 60;
const INPUT_HEIGHT = 50;
const TASK_HEIGHT = 52;
const PADDING = 16;
const MAX_WIDTH = 600;

// ============================================
// Drawing helpers
// ============================================

function drawRoundedRect(x, y, w, h, r) {
    const path = sys.path.create();
    sys.path.moveTo(path, x + r, y);
    sys.path.lineTo(path, x + w - r, y);
    sys.path.quadTo(path, x + w, y, x + w, y + r);
    sys.path.lineTo(path, x + w, y + h - r);
    sys.path.quadTo(path, x + w, y + h, x + w - r, y + h);
    sys.path.lineTo(path, x + r, y + h);
    sys.path.quadTo(path, x, y + h, x, y + h - r);
    sys.path.lineTo(path, x, y + r);
    sys.path.quadTo(path, x, y, x + r, y);
    sys.path.close(path);
    sys.canvas.drawPath(path);
}

// ============================================
// Rendering
// ============================================

export function render(state, width, height) {
    const { tasks, inputText, cursorBlink, scrollY, hoveredTask, hoveredButton } = state;

    // Content area
    const contentX = Math.max(PADDING, (width - MAX_WIDTH) / 2);
    const contentW = Math.min(MAX_WIDTH, width - PADDING * 2);

    // Background
    sys.canvas.clear(COLORS.bg);

    // Header
    sys.canvas.setFillColor(COLORS.header);
    sys.canvas.drawRect(0, 0, width, HEADER_HEIGHT);

    sys.canvas.setFillColor(COLORS.headerText);
    sys.canvas.drawText('TODO List', contentX, 38, 24);

    const doneCount = tasks.filter(t => t.done).length;
    const statsText = tasks.length + ' tasks, ' + doneCount + ' done';
    sys.canvas.setFillColor('#FFFFFF80');
    sys.canvas.drawText(statsText, contentX + contentW - 150, 38, 14);

    // Input area
    const inputY = HEADER_HEIGHT + PADDING;

    // Input field background
    sys.canvas.setFillColor(COLORS.inputBg);
    drawRoundedRect(contentX, inputY, contentW - 80, INPUT_HEIGHT, 8);

    // Input border
    sys.canvas.setStrokeColor(COLORS.inputBorder);
    sys.canvas.setStrokeWidth(1.5);
    drawRoundedRect(contentX, inputY, contentW - 80, INPUT_HEIGHT, 8);

    // Input text
    sys.canvas.setFillColor(inputText ? COLORS.text : COLORS.textMuted);
    const displayText = inputText || 'Type a task and press Enter...';
    sys.canvas.drawText(displayText, contentX + 12, inputY + 32, 16);

    // Blinking cursor
    if (Math.floor(cursorBlink * 2) % 2 === 0) {
        const cursorX = contentX + 12 + inputText.length * 8.5;
        sys.canvas.setStrokeColor(COLORS.accent);
        sys.canvas.setStrokeWidth(2);
        sys.canvas.drawLine(cursorX, inputY + 12, cursorX, inputY + 40);
    }

    // Add button
    const btnX = contentX + contentW - 70;
    const btnColor = hoveredButton === 'add' ? COLORS.accentHover : COLORS.accent;
    sys.canvas.setFillColor(btnColor);
    drawRoundedRect(btnX, inputY, 70, INPUT_HEIGHT, 8);
    sys.canvas.setFillColor('#FFFFFF');
    sys.canvas.drawText('Add', btnX + 20, inputY + 32, 16);

    // Task list
    const listY = inputY + INPUT_HEIGHT + PADDING;
    const visibleHeight = height - listY - PADDING;

    // Clip to list area
    sys.canvas.save();
    sys.canvas.clipRect(0, listY, width, visibleHeight);

    let y = listY - scrollY;
    for (let i = 0; i < tasks.length; i++) {
        const task = tasks[i];

        if (y + TASK_HEIGHT < listY - 10) {
            y += TASK_HEIGHT + 8;
            continue;
        }
        if (y > listY + visibleHeight + 10) break;

        // Card background
        const isHovered = hoveredTask === i;
        sys.canvas.setFillColor(task.done ? COLORS.cardDone : COLORS.card);
        drawRoundedRect(contentX, y, contentW, TASK_HEIGHT, 6);

        // Hover effect
        if (isHovered) {
            sys.canvas.setStrokeColor(COLORS.accent);
            sys.canvas.setStrokeWidth(1.5);
            drawRoundedRect(contentX, y, contentW, TASK_HEIGHT, 6);
        }

        // Checkbox
        const cbX = contentX + 14;
        const cbY = y + 14;
        const cbSize = 24;

        if (task.done) {
            sys.canvas.setFillColor(COLORS.success);
            drawRoundedRect(cbX, cbY, cbSize, cbSize, 4);
            // Checkmark
            sys.canvas.setStrokeColor('#FFFFFF');
            sys.canvas.setStrokeWidth(2.5);
            sys.canvas.drawLine(cbX + 5, cbY + 12, cbX + 10, cbY + 18);
            sys.canvas.drawLine(cbX + 10, cbY + 18, cbX + 19, cbY + 7);
        } else {
            sys.canvas.setStrokeColor(COLORS.inputBorder);
            sys.canvas.setStrokeWidth(1.5);
            drawRoundedRect(cbX, cbY, cbSize, cbSize, 4);
        }

        // Task title
        sys.canvas.setFillColor(task.done ? COLORS.textDone : COLORS.text);
        sys.canvas.drawText(task.title, contentX + 48, y + 33, 15);

        // Delete button
        const delX = contentX + contentW - 36;
        const delBtnHover = hoveredButton === 'delete:' + task.id;
        sys.canvas.setFillColor(delBtnHover ? COLORS.dangerHover : COLORS.danger);
        sys.canvas.drawCircle(delX, y + TASK_HEIGHT / 2, 12);
        sys.canvas.setStrokeColor('#FFFFFF');
        sys.canvas.setStrokeWidth(2);
        sys.canvas.drawLine(delX - 5, y + TASK_HEIGHT / 2 - 5, delX + 5, y + TASK_HEIGHT / 2 + 5);
        sys.canvas.drawLine(delX - 5, y + TASK_HEIGHT / 2 + 5, delX + 5, y + TASK_HEIGHT / 2 - 5);

        y += TASK_HEIGHT + 8;
    }

    sys.canvas.restore();

    // Clear completed button (at bottom)
    if (doneCount > 0) {
        const clearY = height - 44;
        const clearW = 180;
        const clearX = (width - clearW) / 2;
        const clearHover = hoveredButton === 'clear';
        sys.canvas.setFillColor(clearHover ? COLORS.dangerHover : '#E0E0E0');
        drawRoundedRect(clearX, clearY, clearW, 34, 6);
        sys.canvas.setFillColor(clearHover ? '#FFFFFF' : COLORS.textMuted);
        sys.canvas.drawText('Clear completed', clearX + 22, clearY + 22, 13);
    }

    // Empty state
    if (tasks.length === 0) {
        sys.canvas.setFillColor(COLORS.textMuted);
        sys.canvas.drawText('No tasks yet. Add one above!', contentX + contentW / 2 - 120, listY + 60, 16);
    }
}

// ============================================
// Input handling
// ============================================

// Printable character map for scancodes (SDL2 scancodes)
// a-z: 4-29, 0: 39, 1-9: 30-38, space: 44
function scancodeToChar(scancode, shift) {
    if (scancode >= 4 && scancode <= 29) {
        const c = String.fromCharCode(97 + scancode - 4);
        return shift ? c.toUpperCase() : c;
    }
    if (scancode === 39) return shift ? ')' : '0';
    if (scancode >= 30 && scancode <= 38) {
        const digits = '123456789';
        const shifted = '!@#$%^&*(';
        return shift ? shifted[scancode - 30] : digits[scancode - 30];
    }
    if (scancode === 44) return ' ';
    if (scancode === 55) return shift ? '.' : '.';
    if (scancode === 54) return shift ? '<' : ',';
    if (scancode === 56) return shift ? '?' : '/';
    if (scancode === 45) return shift ? '_' : '-';
    if (scancode === 46) return shift ? '+' : '=';
    if (scancode === 51) return shift ? ':' : ';';
    if (scancode === 52) return shift ? '"' : "'";
    return null;
}

export function handleKeyboard(input, state, actions) {
    // Check for typed characters (scan through common scancodes)
    for (let sc = 4; sc <= 56; sc++) {
        if (sys.input.isKeyPressed(sc)) {
            const ch = scancodeToChar(sc, input.keyboard.shift);
            if (ch !== null) {
                state.inputText += ch;
                state.cursorBlink = 0;
            }
        }
    }

    // Backspace (scancode 42)
    if (sys.input.isKeyPressed(42) && state.inputText.length > 0) {
        state.inputText = state.inputText.slice(0, -1);
        state.cursorBlink = 0;
    }

    // Enter (scancode 40)
    if (sys.input.isKeyPressed(40)) {
        actions.addTask(state.inputText);
        state.inputText = '';
        state.cursorBlink = 0;
    }
}

export function handleMouse(input, state, actions, width, height) {
    const mx = input.mouse.x;
    const my = input.mouse.y;

    // Content area
    const contentX = Math.max(PADDING, (width - MAX_WIDTH) / 2);
    const contentW = Math.min(MAX_WIDTH, width - PADDING * 2);

    const inputY = HEADER_HEIGHT + PADDING;
    const listY = inputY + INPUT_HEIGHT + PADDING;

    // Reset hover state
    state.hoveredTask = -1;
    state.hoveredButton = '';

    // Check add button hover
    const btnX = contentX + contentW - 70;
    if (mx >= btnX && mx <= btnX + 70 && my >= inputY && my <= inputY + INPUT_HEIGHT) {
        state.hoveredButton = 'add';
    }

    // Check clear completed button hover
    const doneCount = state.tasks.filter(t => t.done).length;
    if (doneCount > 0) {
        const clearW = 180;
        const clearX = (width - clearW) / 2;
        const clearY = height - 44;
        if (mx >= clearX && mx <= clearX + clearW && my >= clearY && my <= clearY + 34) {
            state.hoveredButton = 'clear';
        }
    }

    // Check task hover / delete button hover
    let y = listY - state.scrollY;
    for (let i = 0; i < state.tasks.length; i++) {
        if (mx >= contentX && mx <= contentX + contentW && my >= y && my <= y + TASK_HEIGHT) {
            state.hoveredTask = i;

            // Check delete button
            const delX = contentX + contentW - 36;
            const dx = mx - delX;
            const dy = my - (y + TASK_HEIGHT / 2);
            if (dx * dx + dy * dy <= 144) { // radius 12
                state.hoveredButton = 'delete:' + state.tasks[i].id;
            }
        }
        y += TASK_HEIGHT + 8;
    }

    // Handle scroll
    if (input.mouse.wheelY) {
        state.scrollY -= input.mouse.wheelY * 30;
        const maxScroll = Math.max(0, state.tasks.length * (TASK_HEIGHT + 8) - (height - listY - PADDING - 50));
        state.scrollY = Math.max(0, Math.min(state.scrollY, maxScroll));
    }

    // Handle clicks
    if (input.mouse.leftPressed) {
        const now = Date.now ? Date.now() : 0;
        if (now - state.lastClickTime < 50) return; // debounce
        state.lastClickTime = now;

        if (state.hoveredButton === 'add') {
            actions.addTask(state.inputText);
            state.inputText = '';
            state.cursorBlink = 0;
        } else if (state.hoveredButton === 'clear') {
            actions.clearCompleted();
        } else if (state.hoveredButton.startsWith('delete:')) {
            const id = parseInt(state.hoveredButton.split(':')[1]);
            actions.deleteTask(id);
        } else if (state.hoveredTask >= 0) {
            actions.toggleTask(state.tasks[state.hoveredTask].id);
        }
    }
}
