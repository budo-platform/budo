/// <reference path="../../budo.d.ts" />

import { Gui, button, custom, hSplit, hStack, input, label, vSplit, vStack } from './gui.ts';

interface Rect {
    x: number;
    y: number;
    width: number;
    height: number;
}

interface CircleGeometry {
    x: number;
    y: number;
    radius: number;
}

function activityCircle(rect: Rect): CircleGeometry {
    return {
        x: rect.x + rect.width * 0.5,
        y: rect.y + rect.height * 0.5 - 12,
        radius: Math.min(rect.width, rect.height) * 0.2,
    };
}

const name = { value: 'Ada' };
let greeting = 'Welcome. This view is drawn by a custom element.';
let clicks = 0;

const activity = custom({
    sizing: { width: 'fill', height: 'fill' },
    measure: () => ({ width: 240, height: 180 }),
    draw: function(rect) {
        sys.canvas.setFillColor('#EEF4F3');
        sys.canvas.drawRoundRect(rect.x, rect.y, rect.width, rect.height, 6, 6);

        sys.canvas.setFillColor('#006D77');
        const circle = activityCircle(rect);
        sys.canvas.drawCircle(circle.x, circle.y, circle.radius);

        sys.canvas.setFillColor('#FFFFFF');
        const count = String(clicks);
        const countWidth = sys.canvas.measureText(count, 56);
        sys.canvas.drawText(count, circle.x - countWidth * 0.5, circle.y + 20, 56);

        sys.canvas.setFillColor('#53636B');
        const caption = 'Custom element: click the circle';
        const captionWidth = sys.canvas.measureText(caption, 28);
        sys.canvas.drawText(caption, circle.x - captionWidth * 0.5, circle.y + circle.radius + 64, 28);
    },
    pointer: function(event, rect) {
        if (event.type !== 'press') return false;
        const circle = activityCircle(rect);
        const dx = event.x - circle.x;
        const dy = event.y - circle.y;
        if (dx * dx + dy * dy > circle.radius * circle.radius) return false;
        clicks++;
        return true;
    },
});

const form = vStack([
    label('Retained GUI', { size: 56, color: '#12343B' }),
    label('Elements keep their state; layout is recomputed from the tree.', { size: 28, color: '#6B7780' }),
    label('Your name', { size: 26, color: '#53636B' }),
    input(name, {
        placeholder: 'Type a name',
        onChange: function(value) {
            greeting = value ? 'Hello, ' + value + '.' : 'Hello, stranger.';
        },
    }),
    hStack([
        button('Greet', (): void => {
            greeting = name.value ? 'Hello, ' + name.value + '!' : 'Hello, stranger!';
            clicks++;
        }, { primary: true }),
        button('Reset', (): void => {
            name.value = '';
            greeting = 'The retained model has been reset.';
            clicks = 0;
        }),
    ], { gap: 10 }),
    label('Typing follows your keyboard layout; arrows and shortcuts use physical keys.', {
        size: 26,
        color: '#6B7780',
        sizing: { width: 'fill' },
    }),
], { gap: 10, padding: 24, sizing: { width: 'fill', height: 'fill' } });

const inspector = vStack([
    label('Layout', { size: 36, color: '#12343B' }),
    label('hStack  ·  vStack', { color: '#53636B' }),
    label('hSplit  ·  vSplit', { color: '#53636B' }),
    label('content  /  pixels  /  fill', { color: '#53636B' }),
    label('Drag either pale divider.', { size: 26, color: '#E76F51' }),
], { gap: 9, padding: 18, sizing: { width: 'fill', height: 'fill' } });

const status = custom({
    sizing: { width: 'fill', height: 108 },
    measure: () => ({ width: 600, height: 108 }),
    draw: function(rect) {
        sys.canvas.setFillColor('#12343B');
        sys.canvas.drawRoundRect(rect.x, rect.y, rect.width, rect.height, 6, 6);
        sys.canvas.setFillColor('#FFFFFF');
        sys.canvas.drawText(greeting, rect.x + 32, rect.y + 66, 30);
    },
});

const workspace = hSplit(form, vSplit(activity, inspector, { ratio: 0.64 }), { ratio: 0.58 });
const root = vStack([workspace, status], { gap: 12, padding: 16, sizing: { width: 'fill', height: 'fill' } });
const gui = Gui(root);

function frame(): void {
    const width = sys.window.getWidth();
    const height = sys.window.getHeight();
    sys.canvas.clear('#F7F9F8');
    gui.frame(sys.input.get(), { x: 0, y: 0, width, height });
    sys.animation.requestFrame(frame);
}

sys.animation.requestFrame(frame);