interface Size {
    width: number;
    height: number;
}

interface Rect extends Size {
    x: number;
    y: number;
}

type Dimension = number | 'content' | 'fill';

interface Sizing {
    width?: Dimension;
    height?: Dimension;
}

interface PointerEvent {
    type: 'press' | 'move' | 'release';
    x: number;
    y: number;
}

interface KeyEvent {
    key: string;
    shift: boolean;
    ctrl: boolean;
}

interface CustomSpec {
    sizing?: Sizing;
    focusable?: boolean;
    measure?: (available: Size) => Size;
    draw: (rect: Rect, state: { focused: boolean; hovered: boolean }) => void;
    pointer?: (event: PointerEvent, rect: Rect) => boolean;
    key?: (event: KeyEvent) => void;
    text?: (text: string) => void;
    textEdit?: (edit: { text: string; selectionStart: number; selectionEnd: number }) => void;
    composition?: (composition: { active: boolean; text: string; selectionStart: number; selectionEnd: number }) => void;
    focus?: (focused: boolean) => void;
}

interface StackOptions {
    gap?: number;
    padding?: number;
    sizing?: Sizing;
}

interface SplitOptions {
    ratio?: number;
    gap?: number;
    hitSize?: number;
}

interface LabelOptions {
    size?: number;
    color?: string;
    sizing?: Sizing;
}

interface InputOptions {
    placeholder?: string;
    width?: Dimension;
    onChange?: (value: string) => void;
}

interface ButtonOptions {
    width?: Dimension;
    primary?: boolean;
}

interface KeyMapItem {
    scancode: number;
    normal: string;
    shifted?: string;
}

type Axis = 'horizontal' | 'vertical';

const palette = {
    text: '#24313A',
    muted: '#6B7780',
    surface: '#FFFFFF',
    border: '#C9D1D5',
    accent: '#006D77',
    accentHover: '#005A63',
    focus: '#F4A261',
};

function clamp(value: number, min: number, max: number): number {
    return Math.max(min, Math.min(max, value));
}

function contains(rect: Rect, x: number, y: number): boolean {
    return x >= rect.x && x <= rect.x + rect.width && y >= rect.y && y <= rect.y + rect.height;
}

function inset(rect: Rect, amount: number): Rect {
    return {
        x: rect.x + amount,
        y: rect.y + amount,
        width: Math.max(0, rect.width - amount * 2),
        height: Math.max(0, rect.height - amount * 2),
    };
}

interface Element {
    sizing: Sizing;
    focusable: boolean;
    children: Element[];
    rect: Rect;
    focused: boolean;
    hovered: boolean;
    setLayout(layout: (rect: Rect) => void): void;
    measure(available: Size): Size;
    layout(rect: Rect): void;
    draw(): void;
    dispatchPress(event: PointerEvent): Element | null;
    dispatchPointer(event: PointerEvent): void;
    dispatchKey(event: KeyEvent): void;
    dispatchText(text: string): void;
    dispatchTextEdit(edit: { text: string; selectionStart: number; selectionEnd: number }): void;
    dispatchComposition(composition: { active: boolean; text: string; selectionStart: number; selectionEnd: number }): void;
    dispatchFocus(focused: boolean): void;
    updateHover(x: number, y: number): void;
}

function createElement(spec: CustomSpec, children: Element[] = []): Element {
    let layoutChildren: ((rect: Rect) => void) | null = null;
    const element: Element = {
        sizing: spec.sizing || {},
        focusable: spec.focusable === true,
        children,
        rect: { x: 0, y: 0, width: 0, height: 0 },
        focused: false,
        hovered: false,
        setLayout: function(layout) {
            layoutChildren = layout;
        },
        measure: function(available) {
            let natural = { width: 0, height: 0 };
            if (spec.measure) natural = spec.measure(available);
            return {
                width: typeof element.sizing.width === 'number' ? element.sizing.width : natural.width,
                height: typeof element.sizing.height === 'number' ? element.sizing.height : natural.height,
            };
        },
        layout: function(rect) {
            element.rect = rect;
            if (layoutChildren) layoutChildren(rect);
        },
        draw: function() {
            spec.draw(element.rect, { focused: element.focused, hovered: element.hovered });
            for (const child of children) child.draw();
        },
        dispatchPress: function(event) {
            if (!contains(element.rect, event.x, event.y)) return null;
            for (let index = children.length - 1; index >= 0; index--) {
                const target = children[index].dispatchPress(event);
                if (target) return target;
            }
            return spec.pointer && spec.pointer(event, element.rect) ? element : null;
        },
        dispatchPointer: function(event) {
            if (spec.pointer) spec.pointer(event, element.rect);
        },
        dispatchKey: function(event) {
            if (spec.key) spec.key(event);
        },
        dispatchText: function(text) {
            if (spec.text) spec.text(text);
        },
        dispatchTextEdit: function(edit) {
            if (spec.textEdit) spec.textEdit(edit);
        },
        dispatchComposition: function(composition) {
            if (spec.composition) spec.composition(composition);
        },
        dispatchFocus: function(focused) {
            if (spec.focus) spec.focus(focused);
        },
        updateHover: function(x, y) {
            element.hovered = contains(element.rect, x, y);
            for (const child of children) child.updateHover(x, y);
        },
    };
    return element;
}

export function custom(spec: CustomSpec): Element {
    return createElement(spec);
}

function requestedSize(element: Element, available: Size): Size {
    const measured = element.measure(available);
    return {
        width: clamp(measured.width, 0, available.width),
        height: clamp(measured.height, 0, available.height),
    };
}

function stack(axis: Axis, children: Element[], options: StackOptions = {}): Element {
    const gap = options.gap === undefined ? 8 : options.gap;
    const padding = options.padding === undefined ? 0 : options.padding;
    const horizontal = axis === 'horizontal';

    const container = createElement({
        sizing: options.sizing,
        measure: function(available) {
            let main = padding * 2 + gap * Math.max(0, children.length - 1);
            let cross = 0;
            for (const child of children) {
                const size = requestedSize(child, available);
                main += horizontal ? size.width : size.height;
                cross = Math.max(cross, horizontal ? size.height : size.width);
            }
            return horizontal ? { width: main, height: cross + padding * 2 } : { width: cross + padding * 2, height: main };
        },
        draw: function() {},
    }, children);

    container.setLayout((rect: Rect): void => {
        const inner = inset(rect, padding);
        const mainAvailable = horizontal ? inner.width : inner.height;
        const crossAvailable = horizontal ? inner.height : inner.width;
        const sizes = children.map((child: Element) => requestedSize(child, inner));
        let fixed = gap * Math.max(0, children.length - 1);
        let fillCount = 0;

        for (let index = 0; index < children.length; index++) {
            const mode = horizontal ? children[index].sizing.width : children[index].sizing.height;
            if (mode === 'fill') fillCount++;
            else fixed += horizontal ? sizes[index].width : sizes[index].height;
        }

        const fillSize = fillCount > 0 ? Math.max(0, mainAvailable - fixed) / fillCount : 0;
        let cursor = horizontal ? inner.x : inner.y;

        for (let index = 0; index < children.length; index++) {
            const child = children[index];
            const size = sizes[index];
            const mainMode = horizontal ? child.sizing.width : child.sizing.height;
            const crossMode = horizontal ? child.sizing.height : child.sizing.width;
            const mainSize = mainMode === 'fill' ? fillSize : horizontal ? size.width : size.height;
            const crossSize = crossMode === 'fill' || crossMode === undefined
                ? crossAvailable
                : Math.min(crossAvailable, horizontal ? size.height : size.width);

            child.layout(horizontal
                ? { x: cursor, y: inner.y, width: mainSize, height: crossSize }
                : { x: inner.x, y: cursor, width: crossSize, height: mainSize });
            cursor += mainSize + gap;
        }
    });

    return container;
}

export function hStack(children: Element[], options: StackOptions = {}): Element {
    return stack('horizontal', children, options);
}

export function vStack(children: Element[], options: StackOptions = {}): Element {
    return stack('vertical', children, options);
}

function splitter(axis: Axis, first: Element, second: Element, options: SplitOptions = {}): Element {
    let ratio = options.ratio === undefined ? 0.5 : options.ratio;
    const gap = options.gap === undefined ? 10 : options.gap;
    const hitSize = Math.max(gap, options.hitSize === undefined ? 48 : options.hitSize);
    const horizontal = axis === 'horizontal';

    const container = createElement({
        sizing: { width: 'fill', height: 'fill' },
        measure: (available: Size): Size => available,
        draw: function(rect) {
            const position = horizontal ? rect.x + rect.width * ratio : rect.y + rect.height * ratio;
            sys.canvas.setFillColor('#DCE3E5');
            if (horizontal) sys.canvas.drawRoundRect(position - gap * 0.5, rect.y, gap, rect.height, 3, 3);
            else sys.canvas.drawRoundRect(rect.x, position - gap * 0.5, rect.width, gap, 3, 3);
        },
        pointer: function(event, rect) {
            if (event.type === 'press') {
                const position = horizontal ? rect.x + rect.width * ratio : rect.y + rect.height * ratio;
                const coordinate = horizontal ? event.x : event.y;
                return Math.abs(coordinate - position) <= hitSize * 0.5;
            }
            if (event.type === 'move') {
                const start = horizontal ? rect.x : rect.y;
                const length = horizontal ? rect.width : rect.height;
                const coordinate = horizontal ? event.x : event.y;
                ratio = clamp((coordinate - start) / Math.max(1, length), 0.15, 0.85);
                container.layout(rect);
            }
            return true;
        },
    }, [first, second]);

    container.setLayout((rect: Rect): void => {
        if (horizontal) {
            const firstWidth = Math.max(0, rect.width * ratio - gap * 0.5);
            first.layout({ x: rect.x, y: rect.y, width: firstWidth, height: rect.height });
            second.layout({ x: rect.x + firstWidth + gap, y: rect.y, width: Math.max(0, rect.width - firstWidth - gap), height: rect.height });
        } else {
            const firstHeight = Math.max(0, rect.height * ratio - gap * 0.5);
            first.layout({ x: rect.x, y: rect.y, width: rect.width, height: firstHeight });
            second.layout({ x: rect.x, y: rect.y + firstHeight + gap, width: rect.width, height: Math.max(0, rect.height - firstHeight - gap) });
        }
    });

    return container;
}

export function hSplit(first: Element, second: Element, options: SplitOptions = {}): Element {
    return splitter('horizontal', first, second, options);
}

export function vSplit(first: Element, second: Element, options: SplitOptions = {}): Element {
    return splitter('vertical', first, second, options);
}

export function label(text: string, options: LabelOptions = {}): Element {
    const fontSize = options.size === undefined ? 32 : options.size;
    return custom({
        sizing: options.sizing,
        measure: function() {
            return { width: sys.canvas.measureText(text, fontSize), height: fontSize * 1.4 };
        },
        draw: function(rect) {
            sys.canvas.setFillColor(options.color || palette.text);
            sys.canvas.drawText(text, rect.x, rect.y + Math.min(rect.height, fontSize * 1.15), fontSize);
        },
    });
}

interface InputModel {
    value: string;
}

export function input(model: InputModel, options: InputOptions = {}): Element {
    const fontSize = 30;
    const controlHeight = 76;
    const textX = 22;
    const baselineY = 50;
    let caret = model.value.length;
    let blinkStarted = 0;
    let compositionText = '';
    let compositionCaret = 0;

    function moveToPreviousWord(): void {
        while (caret > 0 && /\s/.test(model.value[caret - 1])) caret--;
        while (caret > 0 && !/\s/.test(model.value[caret - 1])) caret--;
    }

    function moveToNextWord(): void {
        while (caret < model.value.length && !/\s/.test(model.value[caret])) caret++;
        while (caret < model.value.length && /\s/.test(model.value[caret])) caret++;
    }

    return custom({
        sizing: { width: options.width === undefined ? 'fill' : options.width, height: controlHeight },
        focusable: true,
        measure: function() { return { width: 360, height: controlHeight }; },
        draw: function(rect, state) {
            caret = clamp(caret, 0, model.value.length);
            sys.canvas.setFillColor(palette.surface);
            sys.canvas.drawRoundRect(rect.x, rect.y, rect.width, rect.height, 5, 5);
            sys.canvas.setStrokeColor(state.focused ? palette.focus : palette.border);
            sys.canvas.setStrokeWidth(state.focused ? 2 : 1);
            sys.canvas.drawRoundRect(rect.x, rect.y, rect.width, rect.height, 5, 5);
            const beforeCaret = model.value.slice(0, caret);
            const afterCaret = model.value.slice(caret);
            const editedText = beforeCaret + compositionText + afterCaret;
            const shown = editedText || options.placeholder || '';
            sys.canvas.setFillColor(editedText ? palette.text : palette.muted);
            sys.canvas.drawText(shown, rect.x + textX, rect.y + baselineY, fontSize);

            if (compositionText) {
                const compositionX = rect.x + textX + sys.canvas.measureText(beforeCaret, fontSize);
                const compositionWidth = sys.canvas.measureText(compositionText, fontSize);
                sys.canvas.setStrokeColor(palette.accent);
                sys.canvas.setStrokeWidth(1);
                sys.canvas.drawLine(compositionX, rect.y + 58,
                    compositionX + compositionWidth, rect.y + 58);
            }

            const elapsed = sys.input.get().totalTime - blinkStarted;
            const blinkOn = Math.floor(elapsed * 2) % 2 === 0;
            if (state.focused && blinkOn) {
                const cursorText = beforeCaret + compositionText.slice(0, compositionCaret);
                const cursorX = rect.x + textX + sys.canvas.measureText(cursorText, fontSize);
                sys.canvas.setStrokeColor(palette.text);
                sys.canvas.setStrokeWidth(1);
                sys.canvas.drawLine(cursorX, rect.y + 20, cursorX, rect.y + 56);
            }

            if (state.focused) {
                const caretText = beforeCaret + compositionText.slice(0, compositionCaret);
                const caretX = rect.x + textX + sys.canvas.measureText(caretText, fontSize);
                sys.input.updateTextInput({
                    text: model.value,
                    selectionStart: caret,
                    selectionEnd: caret,
                    caret: { x: caretX, y: rect.y + 16, width: 1, height: 44 },
                });
            }
        },
        pointer: function(event) {
            if (event.type !== 'press') return false;
            caret = model.value.length;
            blinkStarted = sys.input.get().totalTime;
            return true;
        },
        key: function(event) {
            caret = clamp(caret, 0, model.value.length);

            if (event.key === 'Left') {
                if (event.ctrl) moveToPreviousWord();
                else caret = Math.max(0, caret - 1);
                blinkStarted = sys.input.get().totalTime;
                return;
            }
            if (event.key === 'Right') {
                if (event.ctrl) moveToNextWord();
                else caret = Math.min(model.value.length, caret + 1);
                blinkStarted = sys.input.get().totalTime;
                return;
            }
            if (event.key === 'Backspace' && caret > 0) {
                model.value = model.value.slice(0, caret - 1) + model.value.slice(caret);
                caret--;
            }
            else return;
            blinkStarted = sys.input.get().totalTime;
            if (options.onChange) options.onChange(model.value);
        },
        text: function(text) {
            if (!text || model.value.length >= 40) return;
            const available = 40 - model.value.length;
            const inserted = text.slice(0, available);
            model.value = model.value.slice(0, caret) + inserted + model.value.slice(caret);
            caret += inserted.length;
            blinkStarted = sys.input.get().totalTime;
            if (options.onChange) options.onChange(model.value);
        },
        textEdit: function(edit) {
            model.value = edit.text.slice(0, 40);
            caret = clamp(edit.selectionEnd, 0, model.value.length);
            blinkStarted = sys.input.get().totalTime;
            if (options.onChange) options.onChange(model.value);
        },
        composition: function(composition) {
            compositionText = composition.active ? composition.text : '';
            compositionCaret = composition.active ? composition.selectionEnd : 0;
            blinkStarted = sys.input.get().totalTime;
        },
        focus: function(focused) {
            if (focused) {
                sys.input.startTextInput({
                    text: model.value,
                    selectionStart: caret,
                    selectionEnd: caret,
                    multiline: false,
                });
            } else {
                compositionText = '';
                compositionCaret = 0;
                sys.input.stopTextInput();
            }
        },
    });
}

export function button(text: string, onClick: () => void, options: ButtonOptions = {}): Element {
    const fontSize = 30;
    const controlHeight = 76;
    let armed = false;
    return custom({
        sizing: { width: options.width === undefined ? 'content' : options.width, height: controlHeight },
        focusable: true,
        measure: function() { return { width: sys.canvas.measureText(text, fontSize) + 56, height: controlHeight }; },
        draw: function(rect, state) {
            const primary = options.primary === true;
            let background = state.hovered ? '#E7ECEE' : '#F3F6F7';
            if (primary) background = state.hovered ? palette.accentHover : palette.accent;
            sys.canvas.setFillColor(background);
            sys.canvas.drawRoundRect(rect.x, rect.y, rect.width, rect.height, 5, 5);
            sys.canvas.setStrokeColor(state.focused ? palette.focus : primary ? palette.accent : palette.border);
            sys.canvas.setStrokeWidth(state.focused ? 2 : 1);
            sys.canvas.drawRoundRect(rect.x, rect.y, rect.width, rect.height, 5, 5);
            const textWidth = sys.canvas.measureText(text, fontSize);
            sys.canvas.setFillColor(primary ? '#FFFFFF' : palette.text);
            sys.canvas.drawText(text, rect.x + (rect.width - textWidth) * 0.5, rect.y + 50, fontSize);
        },
        pointer: function(event, rect) {
            if (event.type === 'press') armed = true;
            if (event.type === 'release') {
                if (armed && contains(rect, event.x, event.y)) onClick();
                armed = false;
            }
            return true;
        },
        key: function(event) {
            if (event.key === 'Enter' || event.key === ' ') onClick();
        },
    });
}

const keyMap: KeyMapItem[] = [
    { scancode: 44, normal: ' ' }, { scancode: 42, normal: 'Backspace' }, { scancode: 40, normal: 'Enter' },
    { scancode: 80, normal: 'Left' }, { scancode: 79, normal: 'Right' },
];

interface GuiController {
    root: Element;
    frame(inputState: InputState, bounds: Rect): void;
}

export function Gui(root: Element): GuiController {
    let focused: Element | null = null;
    let active: Element | null = null;
    let pointerWasDown = false;

    function setFocus(element: Element | null): void {
        if (focused) {
            focused.focused = false;
            focused.dispatchFocus(false);
        }
        focused = element;
        if (focused) {
            focused.focused = true;
            focused.dispatchFocus(true);
        }
    }

    function dispatchKeys(shift: boolean, ctrl: boolean): void {
        if (!focused) return;
        for (const item of keyMap) {
            if (!sys.input.isKeyPressed(item.scancode)) continue;
            const key = shift && item.shifted ? item.shifted : item.normal;
            focused.dispatchKey({ key, shift, ctrl });
        }
    }

    return { root, frame: function(inputState, bounds) {
        const pointer = inputState.pointer;
        root.layout(bounds);
        root.updateHover(pointer.x, pointer.y);

        if (pointer.pressed) {
            active = root.dispatchPress({ type: 'press', x: pointer.x, y: pointer.y });
            setFocus(active && active.focusable ? active : null);
        } else if (pointer.down && active) {
            active.dispatchPointer({ type: 'move', x: pointer.x, y: pointer.y });
        } else if (pointerWasDown && !pointer.down && active) {
            active.dispatchPointer({ type: 'release', x: pointer.x, y: pointer.y });
            active = null;
        }

        pointerWasDown = pointer.down;
        if (focused && inputState.textEdit) focused.dispatchTextEdit(inputState.textEdit);
        if (focused && inputState.composition.changed)
            focused.dispatchComposition(inputState.composition);
        if (!inputState.textEdit)
            dispatchKeys(inputState.keyboard.shift, inputState.keyboard.ctrl);
        if (focused && inputState.text) focused.dispatchText(inputState.text);
        root.draw();
    }};
}