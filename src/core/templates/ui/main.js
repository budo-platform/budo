/// <reference path="./budo.d.ts" />
// A budo-ui starter. The interface is drawn from the app's data every frame;
// controls return their results right away, and the library animates the
// rest. ui/README.md lists every widget.

import { createUI, themes } from './ui/budo-ui.js';

const ui = createUI();
ui.setTheme('system'); // follow the system dark mode
const dp = ui.dp;

const name = { value: '' };
let theme = 'System';
let notifications = true;
let volume = 0.6;

function frame() {
    ui.begin(sys.input.get());
    ui.clear();

    // One centered column, at most 480 dp wide.
    const page = ui.inset(ui.bounds, dp(24));
    const width = Math.min(page.width, dp(480));
    const column = { ...page, x: page.x + (page.width - width) / 2, width };
    const [title, nameRow, themeRow, notifyRow, volumeRow, , actions] = ui.rows(column,
        [dp(48), dp(48), dp(44), dp(48), dp(48), { weight: 1 }, dp(48)], dp(12));

    ui.label('Hello, Budo', title, { size: dp(28) });
    ui.field('name', nameRow, name, { placeholder: 'Your name' });

    const picked = ui.segmented('theme', themeRow, ['System', 'Light', 'Dark'], theme, { label: 'Theme' });
    if (picked !== theme) {
        theme = picked;
        ui.setTheme(picked === 'System' ? 'system' : picked === 'Dark' ? themes.dark : themes.light);
    }

    notifications = ui.toggle('notifications', 'Notifications', notifyRow, notifications);

    const [volumeLabel, volumeSlider] = ui.columns(volumeRow, [dp(110), { weight: 1 }], dp(12));
    ui.label('Volume', volumeLabel, { color: ui.colors.muted });
    volume = ui.slider('volume', volumeSlider, volume, { label: 'Volume' });

    if (ui.button('hello', 'Say hello', actions, { primary: true }))
        ui.toast(name.value ? 'Hello, ' + name.value + '!' : 'Hello!');

    ui.end();
    ui.nextFrame(frame); // the next frame while something moves, otherwise on the next input
}

sys.animation.requestFrame(frame);
