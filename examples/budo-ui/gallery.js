/** Widget gallery: every budo-ui widget, by section. Used by the demo app and
 *  by the snapshot tests (budo-ui.snapshots.test.mjs), so it has no side
 *  effects at import and keeps its state in one object. */

import { ICONS } from './lib/icons.js';

export const SECTIONS = ['Inputs', 'Choices', 'Feedback', 'Overlays', 'Text', 'Data', 'Icons', 'Surfaces', 'Motion'];

/** Fresh gallery state (the app owns it, like any budo-ui data). */
export function createGalleryState() {
    return {
        section: 'Inputs',
        name: { value: 'Ada Lovelace' },
        email: { value: '' },
        title: 'Analytical engine',
        notifications: true, compact: false, terms: false,
        size: 'Medium', period: 'Week', view: 'Grid', color: 'Teal', volume: 0.6,
        progress: 0.35, loading: false,
        dialogOpen: false, sheetOpen: false, lastAction: 'None yet',
        pages: ['tiles'],
        notes: { value: 'Text areas wrap, scroll, and select with the mouse (double-click a word, triple-click a line).\n' +
            'Alt or Ctrl moves by words, Up and Down keep the column, and Ctrl+Z undoes a whole run of typing.' },
        planets: PLANETS.slice(), planetSort: null, planetId: null, fileId: 'main',
        mail: ['Invoice from Babbage & Co', 'Lunch on Friday?', 'Engine schematics v2', 'Weekly report', 'Conference tickets'],
    };
}

/** Draw the gallery into `rect`. @param {any} ui @param {any} rect @param {ReturnType<typeof createGalleryState>} state */
export function drawGallery(ui, rect, state) {
    const dp = ui.dp;
    const narrow = rect.width < dp(640);
    const [nav, body] = narrow
        ? [{ ...rect, height: dp(44) }, { ...rect, y: rect.y + dp(56), height: rect.height - dp(56) }]
        : ui.columns(rect, [dp(180), { weight: 1 }], dp(16));
    if (narrow) state.section = ui.segmented('gallery:nav', nav, SECTIONS.map(s => ({ value: s, label: s.slice(0, 5) })), state.section, { label: 'Section' });
    else ui.panel(nav, inner => {
        const rows = ui.rows(inner, SECTIONS.map(() => dp(42)), dp(4));
        SECTIONS.forEach((name, i) => {
            if (ui.choice('gallery:section:' + name, name, rows[i], state.section === name)) state.section = name;
        });
    });
    ui.panel(body, inner => {
        const height = sectionHeight(ui, state.section, inner.width);
        ui.scroll('gallery:body', inner, height, content => {
            ui.scope(state.section, () => SECTION_DRAW[state.section](ui, { ...content, height }, state));
        });
    });
}

const HEIGHTS = { Inputs: 420, Choices: 640, Feedback: 330, Overlays: 300, Text: 560, Data: 470, Icons: 420, Surfaces: 430, Motion: 420 };
const sectionHeight = (ui, section) => ui.dp(HEIGHTS[section]);

/** A section title and its caption. */
function heading(ui, rect, title, caption) {
    const [head, text] = ui.rows(rect, [ui.dp(30), ui.dp(24)], 0);
    ui.label(title, head, { size: ui.sp(22) });
    ui.label(caption, text, { size: ui.sp(15), color: ui.colors.muted });
}

const TILES = [
    { id: 'teal', name: 'Teal', color: '#2E9E8F' }, { id: 'amber', name: 'Amber', color: '#E8A33D' },
    { id: 'rose', name: 'Rose', color: '#D0607A' }, { id: 'slate', name: 'Slate', color: '#5D7180' },
];

/** 1,000 generated rows: the table only draws the visible ones. */
const PLANETS = Array.from({ length: 1000 }, (_, i) => ({
    id: 'planet-' + i,
    name: ['Kepler', 'Gliese', 'Trappist', 'Wolf', 'Ross', 'Luyten'][i % 6] + '-' + (100 + i * 7 % 900) + String.fromCharCode(98 + i % 6),
    distance: Math.round(4 + (i * 37 % 1200) / 1.7),
    radius: (0.5 + (i * 13 % 40) / 10).toFixed(1),
}));

const FILES = [
    { id: 'src', label: 'src', icon: 'folder', expanded: true, children: [
        { id: 'main', label: 'main.js', icon: 'file' },
        { id: 'ui', label: 'ui', icon: 'folder', children: [
            { id: 'core', label: 'core.js', icon: 'file' }, { id: 'theme', label: 'theme.js', icon: 'file' },
            { id: 'widgets', label: 'widgets', icon: 'folder', children: [{ id: 'basic', label: 'basic.js', icon: 'file' }] },
        ] },
    ] },
    { id: 'assets', label: 'assets', icon: 'folder', children: [{ id: 'logo', label: 'logo.svg', icon: 'star' }] },
    { id: 'app', label: 'app.json', icon: 'info' },
];

const SECTION_DRAW = {
    Inputs(ui, rect, state) {
        const dp = ui.dp;
        const [top, buttons, fields, inline, status] = ui.rows(rect, [dp(62), dp(48), dp(48), dp(48), dp(40)], dp(14));
        heading(ui, top, 'Buttons and fields', 'Hover, press, Tab, and type: everything animates.');
        const [primary, secondary, ghost, danger, disabled] = ui.columns(buttons, [1, 1, 1, 1, 1].map(weight => ({ weight })), dp(10));
        if (ui.button('save', 'Save', primary, { primary: true })) { state.lastAction = 'Saved'; ui.toast('Saved', { kind: 'success' }); }
        if (ui.button('share', 'Share', secondary)) state.lastAction = 'Shared';
        if (ui.button('undo', 'Undo', ghost, { variant: 'ghost' })) state.lastAction = 'Undone';
        if (ui.button('delete', 'Delete', danger, { variant: 'danger' })) state.lastAction = 'Deleted';
        ui.button('locked', 'Locked', disabled, { enabled: false });
        ui.tooltip('save', primary, 'Save the profile');
        const [name, email] = ui.columns(fields, [{ weight: 1 }, { weight: 1 }], dp(10));
        ui.field('name', name, state.name, { label: 'Name' });
        ui.field('email', email, state.email, { placeholder: 'Email' });
        const renamed = ui.inlineEdit('title', state.title, inline, { label: 'Project title' });
        if (renamed !== null) state.title = renamed;
        ui.label('Last action: ' + state.lastAction, status, { size: ui.sp(16), color: ui.colors.accent });
    },

    Choices(ui, rect, state) {
        const dp = ui.dp;
        const [top, toggles, radios, segments, tabsRow, selectRow, sliderRow] =
            ui.rows(rect, [dp(62), dp(96), dp(120), dp(40), dp(44), dp(46), dp(40)], dp(16));
        heading(ui, top, 'Choices', 'Switches, radios, segments, tabs, selects, and sliders.');
        const [a, b, c] = ui.rows(toggles, [{ weight: 1 }, { weight: 1 }, { weight: 1 }], 0);
        state.notifications = ui.toggle('notifications', 'Notifications', a, state.notifications);
        state.compact = ui.toggle('compact', 'Compact rows', b, state.compact);
        state.terms = ui.checkbox('terms', 'I accept the terms', c, state.terms);
        state.size = ui.radio('size', { ...radios, width: Math.min(radios.width, dp(260)) },
            ['Small', 'Medium', 'Large', { value: 'Huge', disabled: true }], state.size, { label: 'Size' });
        state.period = ui.segmented('period', { ...segments, width: Math.min(segments.width, dp(360)) },
            ['Day', 'Week', 'Month', 'Year'], state.period, { label: 'Period' });
        state.view = ui.tabs('view', tabsRow, ['Grid', 'List', 'Timeline'], state.view);
        state.color = ui.select('color', { ...selectRow, width: Math.min(selectRow.width, dp(260)) },
            ['Teal', 'Amber', 'Rose', 'Slate'], state.color, { label: 'Color' });
        state.volume = ui.slider('volume', sliderRow, state.volume, { label: 'Volume' });
    },

    Feedback(ui, rect, state) {
        const dp = ui.dp;
        const [top, bar, busy, skeletons, actions] = ui.rows(rect, [dp(62), dp(30), dp(30), dp(60), dp(44)], dp(16));
        heading(ui, top, 'Feedback', 'Progress glides, busy states move, toasts stack.');
        ui.progress('progress', { ...bar, y: bar.y + dp(12), height: dp(6) }, state.progress, { label: 'Upload' });
        const [spin, indeterminate] = ui.columns(busy, [dp(30), { weight: 1 }], dp(12));
        if (state.loading) ui.spinner(spin, { label: 'Loading' });
        ui.progress('busy', { ...indeterminate, y: indeterminate.y + dp(12), height: dp(6) }, state.loading ? null : 1);
        const [line1, line2] = ui.rows(skeletons, [dp(18), dp(18)], dp(12));
        ui.skeleton({ ...line1, width: line1.width * 0.7 });
        ui.skeleton({ ...line2, width: line2.width * 0.45 });
        const [step, toggle, toastButton] = ui.columns(actions, [{ weight: 1 }, { weight: 1 }, { weight: 1 }], dp(10));
        if (ui.button('step', 'Progress +', step)) state.progress = state.progress >= 1 ? 0 : Math.min(1, state.progress + 0.2);
        if (ui.button('loading', state.loading ? 'Stop' : 'Load', toggle)) state.loading = !state.loading;
        if (ui.button('toast', 'Toast', toastButton)) ui.toast('Hello from budo-ui');
    },

    Overlays(ui, rect, state) {
        const dp = ui.dp;
        const [top, row, area, status] = ui.rows(rect, [dp(62), dp(46), dp(110), dp(30)], dp(16));
        heading(ui, top, 'Overlays', 'Menus, context menus, dialogs, and sheets on frosted glass.');
        const [menuRect, dialogRect, sheetRect] = ui.columns(row, [{ weight: 1 }, { weight: 1 }, { weight: 1 }], dp(10));
        const action = ui.menu('actions', menuRect, 'Actions', [
            { value: 'duplicate', label: 'Duplicate' }, { value: 'rename', label: 'Rename' },
            { value: 'archive', label: 'Archive', disabled: true }, { value: 'delete', label: 'Delete', danger: true }]);
        if (action) state.lastAction = action;
        if (ui.button('open-dialog', 'Dialog', dialogRect)) state.dialogOpen = true;
        if (ui.button('open-sheet', 'Sheet', sheetRect)) state.sheetOpen = true;
        ui.fill(area, ui.colors.raised);
        ui.label('Right-click or long-press here', ui.inset(area, dp(14)), { color: ui.colors.muted, size: ui.sp(16) });
        const context = ui.contextMenu('area', area, [{ value: 'cut', label: 'Cut' }, { value: 'copy', label: 'Copy' }, { value: 'paste', label: 'Paste' }]);
        if (context) state.lastAction = context;
        ui.label('Last action: ' + state.lastAction, status, { size: ui.sp(16), color: ui.colors.accent });
        if (ui.dialog('dialog', state.dialogOpen, content => {
            ui.paragraph('Delete the analytical engine? This cannot be undone.', content, { size: ui.sp(17), color: ui.colors.muted });
            const [cancel, confirm] = ui.columns({ ...content, y: content.y + content.height - dp(44), height: dp(44) },
                [{ weight: 1 }, { weight: 1 }], dp(10));
            if (ui.button('cancel', 'Cancel', cancel)) state.dialogOpen = false;
            if (ui.button('confirm', 'Delete', confirm, { primary: true })) {
                state.dialogOpen = false;
                state.lastAction = 'deleted';
                ui.toast('Deleted', { kind: 'danger' });
            }
        }, { title: 'Delete project', width: dp(420), height: dp(220) })) state.dialogOpen = false;
        if (ui.sheet('sheet', state.sheetOpen, content => {
            const [label, option, done] = ui.rows(content, [dp(30), dp(48), dp(48)], dp(12));
            ui.label('Share settings', label, { size: ui.sp(20) });
            state.notifications = ui.toggle('sheet-notify', 'Notify collaborators', option, state.notifications);
            if (ui.button('done', 'Done', done, { primary: true })) state.sheetOpen = false;
        }, { height: dp(240), title: 'Share settings' })) state.sheetOpen = false;
    },

    Text(ui, rect, state) {
        const dp = ui.dp;
        const [top, paragraph, rich, notes] = ui.rows(rect, [dp(62), dp(120), dp(120), dp(170)], dp(16));
        heading(ui, top, 'Text', 'Wrapped paragraphs, rich text with mixed styles, and a multiline editor.');
        ui.paragraph('Paragraphs wrap to their width, break long words, align left, center, or right, and stop ' +
            'after a number of lines with an ellipsis when asked. This one is limited to three lines, so the end of ' +
            'this sentence will not be visible at all, however wide the window is.', paragraph,
            { size: ui.sp(17), color: ui.colors.muted, maxLines: 3 });
        ui.richText([
            { text: 'Rich text ', size: ui.sp(24), color: ui.colors.accent },
            'mixes sizes and colors in one paragraph: ',
            { text: 'important words', color: ui.colors.danger },
            ' stand out, and lines grow to fit their ',
            { text: 'largest', size: ui.sp(28) },
            ' span while baselines stay aligned.',
        ], rich, { size: ui.sp(17), color: ui.colors.ink });
        ui.textArea('notes', notes, state.notes, { label: 'Notes', placeholder: 'Write some notes' });
    },

    Data(ui, rect, state) {
        const dp = ui.dp;
        const [top, body] = ui.rows(rect, [dp(62), dp(390)], dp(16));
        heading(ui, top, 'Data', 'A sortable table of 1,000 rows and a tree; only visible rows are drawn.');
        const [left, right] = ui.columns(body, [{ weight: 3 }, { weight: 2 }], dp(16));
        const result = ui.table('planets', left, {
            label: 'Planets',
            columns: [
                { key: 'name', label: 'Name', sortable: true, width: { weight: 2 } },
                { key: 'distance', label: 'Light years', sortable: true, align: 'right', width: { weight: 1 } },
                { key: 'radius', label: 'Radius', sortable: true, align: 'right', width: 90 },
            ],
            rowCount: state.planets.length,
            row: index => state.planets[index],
            rowId: index => state.planets[index].id,
            selectedId: state.planetId, sort: state.planetSort,
        });
        state.planetId = result.selectedId;
        if (result.sort) {
            // The app owns the rows: it sorts them when the header asks.
            state.planetSort = result.sort;
            const { key, descending } = result.sort;
            const sign = descending ? -1 : 1;
            state.planets.sort((a, b) => sign * (typeof a[key] === 'string' && isNaN(+a[key])
                ? a[key].localeCompare(b[key]) : +a[key] - +b[key]));
        }
        if (result.activatedId) state.lastAction = 'Opened ' + result.activatedId;
        state.fileId = ui.tree('files', right, FILES, { selectedId: state.fileId, label: 'Files' }).selectedId;
    },

    Icons(ui, rect, state) {
        const dp = ui.dp;
        const [top, actions, grid] = ui.rows(rect, [dp(62), dp(48), { weight: 1 }], dp(16));
        heading(ui, top, 'Icons', 'SVG paths drawn in the theme colors; ui.registerIcon adds your own.');
        const [create, edit, remove, more, search] = ui.columns(actions,
            [{ content: ui.buttonWidth('New') + dp(32) }, dp(48), dp(48), dp(48), dp(48)], dp(10));
        if (ui.button('new', 'New', create, { primary: true, icon: 'plus' })) state.lastAction = 'Created';
        if (ui.iconButton('edit', 'edit', edit, { label: 'Edit' })) state.lastAction = 'Edited';
        if (ui.iconButton('remove', 'trash', remove, { label: 'Delete', variant: 'danger' })) state.lastAction = 'Deleted';
        if (ui.iconButton('more', 'more', more, { label: 'More', variant: 'secondary' })) state.lastAction = 'More';
        ui.iconButton('search', 'search', search, { label: 'Search', enabled: false });
        const names = Object.keys(ICONS);
        const columns = Math.max(1, Math.floor(grid.width / dp(96)));
        const cell = grid.width / columns;
        names.forEach((name, index) => {
            const x = grid.x + (index % columns) * cell, y = grid.y + Math.floor(index / columns) * dp(64);
            ui.icon(name, { x: x + (cell - dp(26)) / 2, y, width: dp(26), height: dp(26) });
            const size = ui.sp(13), width = ui.textWidth(name, size);
            ui.label(name, { x: x + (cell - width) / 2, y: y + dp(30), width, height: dp(20) }, { size, color: ui.colors.muted });
        });
    },

    Surfaces(ui, rect) {
        const dp = ui.dp;
        const [top, cards, elevated] = ui.rows(rect, [dp(62), dp(200), dp(100)], dp(16));
        heading(ui, top, 'Surfaces', 'Gradients, frosted glass, layers that fade as a whole, and elevation.');
        const [gradient, glass, layer] = ui.columns(cards, [{ weight: 1 }, { weight: 1 }, { weight: 1 }], dp(12));
        if (typeof sys.canvas.setGradient === 'function') {
            sys.canvas.setFillColor(ui.colors.accent);
            sys.canvas.setGradient('linear', gradient.x, gradient.y, gradient.x + gradient.width, gradient.y + gradient.height,
                [ui.colors.accent, ui.colors.highlight]);
            sys.canvas.drawRoundRect(gradient.x, gradient.y, gradient.width, gradient.height, dp(14), dp(14));
            sys.canvas.setGradient(null);
        } else ui.fill(gradient, ui.colors.accent, dp(14));
        ui.label('Gradient', ui.inset(gradient, dp(14)), { color: ui.colors.accentInk });
        // Stripes under the glass show the blur.
        for (let i = 0; i < 6; i++)
            ui.fill({ x: glass.x + i * glass.width / 6, y: glass.y, width: glass.width / 12, height: glass.height },
                i % 2 ? ui.colors.highlight : ui.colors.accent, 0);
        ui.glass(ui.inset(glass, dp(18)), { radius: dp(14) });
        ui.label('Glass', ui.inset(glass, dp(30)));
        ui.layer(layer, { opacity: 0.5 }, () => {
            ui.fill(layer, ui.colors.accent, dp(14));
            ui.fill(ui.inset(layer, dp(30)), ui.colors.highlight, dp(10));
        });
        ui.label('Layer at 50%', ui.inset(layer, dp(14)));
        // Room around the cards for the deepest shadow, which the scroll area would clip.
        const shelf = { x: elevated.x + dp(24), y: elevated.y, width: elevated.width - dp(48), height: elevated.height - dp(28) };
        const levels = ui.columns(shelf, [1, 2, 3].map(() => ({ weight: 1 })), dp(32));
        levels.forEach((card, index) => {
            ui.shadow(card, index + 1, { radius: dp(12) });
            ui.fill(card, ui.colors.surface, dp(12));
            ui.label('Elevation ' + (index + 1), ui.inset(card, dp(14)));
        });
    },

    Motion(ui, rect, state) {
        const dp = ui.dp;
        const [top, body] = ui.rows(rect, [dp(62), dp(330)], dp(16));
        heading(ui, top, 'Motion', 'Swipe rows away; tiles morph into pages; drag from the edge to go back.');
        const [mail, pages] = ui.columns(body, [{ weight: 1 }, { weight: 1 }], dp(16));
        // Swipe to delete.
        const items = state.mail.map((title, i) => ({ id: 'mail' + i + ':' + title, title, subtitle: 'Swipe left to delete',
            swipe: { label: 'Delete', danger: true } }));
        const result = ui.list('mail', mail, items, { rowHeight: dp(58), emptyText: 'Inbox zero' });
        if (result.swipedId) {
            state.mail = state.mail.filter((_, i) => items[i].id !== result.swipedId);
            ui.toast('Deleted');
        }
        // A navigation stack with a shared element.
        ui.fill(pages, ui.colors.raised);
        if (ui.navigator('pages', pages, state.pages, route => {
            const inner = ui.inset(pages, dp(12));
            if (route === 'tiles') {
                const cells = ui.columns({ ...inner, height: dp(120) }, TILES.map(() => ({ weight: 1 })), dp(8));
                TILES.forEach((tile, i) => {
                    ui.shared(tile.id, cells[i], r => ui.fill(r, tile.color, dp(10)));
                    if (ui.interact('tile:' + tile.id, cells[i], { a11y: { role: 'button', label: 'Open ' + tile.name } }).clicked)
                        state.pages = [...state.pages, tile.id];
                });
                ui.label('Tap a tile', { ...inner, y: inner.y + dp(130), height: dp(30) }, { color: ui.colors.muted, size: ui.sp(16) });
            } else {
                const tile = TILES.find(item => item.id === route);
                const [bar, hero] = ui.rows(inner, [dp(40), { weight: 1 }], dp(10));
                if (ui.button('back', 'Back', { ...bar, width: dp(90) }, { variant: 'ghost' }))
                    state.pages = state.pages.slice(0, -1);
                ui.shared(tile.id, hero, r => ui.fill(r, tile.color, dp(16)));
                ui.label(tile.name, ui.inset(hero, dp(16)), { color: '#FFFFFF', size: ui.sp(28) });
            }
        })) state.pages = state.pages.slice(0, -1);
    },
};
