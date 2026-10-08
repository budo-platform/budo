import { createUI, themes, withAlpha } from './budo-ui.js';
import { createGalleryState, drawGallery } from './gallery.js';

const ui = createUI();
ui.setTheme('system'); // follow the system dark mode until the user picks a theme
const dp = ui.dp;
let themeName = 'System';
let mode = 'Workspace'; // or 'Gallery'
const gallery = createGalleryState();
let toast = null; // { text, until }
const search = { value: '' };
const title = { value: 'Canvas workspace' };
const crtProgram = sys.gl.createProgram('crt.vert', 'crt.frag');
let crtEnabled = false;
const items = [
    { id: 'dashboard', name: 'Dashboard', type: 'Overview', enabled: true, level: 0.74 },
    { id: 'reports', name: 'Reports', type: 'Analytics', enabled: true, level: 0.42 },
    { id: 'settings', name: 'Settings', type: 'Preferences', enabled: false, level: 0.28 },
    { id: 'studio', name: 'Studio', type: 'Workspace', enabled: true, level: 0.91 },
    { id: 'archive', name: 'Archive', type: 'Library', enabled: false, level: 0.16 },
    { id: 'calendar', name: 'Calendar', type: 'Schedule', enabled: true, level: 0.58 },
    { id: 'inbox', name: 'Inbox', type: 'Messages', enabled: true, level: 0.35 },
    { id: 'assets', name: 'Assets', type: 'Media', enabled: false, level: 0.66 },
];
const groups = [
    { id: 'workspaces', name: 'Workspaces', items: items.slice(0, 4) },
    { id: 'utilities', name: 'Utilities', items: items.slice(4) },
];
let selectedId = 'dashboard';
let message = 'Ready';
let nextItemId = 1;
let preview = { id: null, since: 0 }; // the preview "loads" for a moment after each selection

function selectedItem() {
    return groups.flatMap(group => group.items).find(item => item.id === selectedId) || items[0];
}

function moveItem(fromId, toId) {
    const sourceGroup = groups.find(group => group.items.some(item => item.id === fromId));
    if (!sourceGroup) return;
    const from = sourceGroup.items.findIndex(item => item.id === fromId);
    const to = sourceGroup.items.findIndex(item => item.id === toId);
    if (from < 0 || to < 0) return;
    sourceGroup.items.splice(to, 0, sourceGroup.items.splice(from, 1)[0]);
    message = 'Moved ' + sourceGroup.items[to].name;
}

function sidebar(rect) {
    ui.panel(rect, inner => {
        const [heading, filter, list] = ui.rows(inner, [dp(38), dp(48), { weight: 1 }], dp(10));
        const count = groups.reduce((sum, group) => sum + group.items.length, 0);
        ui.label('LIBRARY  /  ' + count, heading, { size: dp(19), color: ui.colors.muted });
        ui.field('search', filter, search, { placeholder: 'Filter views' });
        const query = search.value.toLowerCase();
        const visible = groups.flatMap(group => {
            const children = group.items.filter(item => group.name.toLowerCase().includes(query) ||
                item.name.toLowerCase().includes(query));
            if (!children.length && !group.name.toLowerCase().includes(query)) return [];
            return [
                {
                    id: 'group:' + group.id, title: group.name, kind: 'group', height: dp(50),
                    action: true, actionLabel: '+', actionName: 'Add a view to ' + group.name
                },
                ...children.map(item => ({
                    id: item.id, title: item.name, subtitle: item.type,
                    kind: 'item', groupId: group.id, depth: 1, height: dp(60),
                    action: true, actionLabel: '-', actionName: 'Remove ' + item.name
                })),
            ];
        });
        const result = ui.list('library', list, visible,
            { selectedId, reorder: true, emptyText: 'No matching views' });
        if (result.selectedId) {
            const group = groups.find(entry => 'group:' + entry.id === result.selectedId);
            const selected = group ? group.items[0] : groups.flatMap(entry => entry.items)
                .find(item => item.id === result.selectedId);
            if (selected) {
                selectedId = selected.id;
                message = 'Selected ' + selected.name;
            }
        }
        if (result.actionId) {
            const group = groups.find(entry => 'group:' + entry.id === result.actionId);
            if (group) {
                const item = {
                    id: 'new:' + nextItemId++, name: 'New view', type: 'Workspace',
                    enabled: true, level: 0.5
                };
                group.items.push(item);
                selectedId = item.id;
                message = 'Added view to ' + group.name;
            } else {
                const owner = groups.find(entry => entry.items.some(item => item.id === result.actionId));
                if (owner && count > 1) {
                    owner.items.splice(owner.items.findIndex(item => item.id === result.actionId), 1);
                    if (selectedId === result.actionId) selectedId = groups.flatMap(entry => entry.items)[0].id;
                    message = 'Removed view';
                }
            }
        }
        if (result.move) {
            const sourceGroup = groups.find(group => 'group:' + group.id === result.move.fromId);
            const targetGroup = groups.find(group => 'group:' + group.id === result.move.toId);
            if (sourceGroup && targetGroup) {
                groups.splice(groups.indexOf(targetGroup), 0, groups.splice(groups.indexOf(sourceGroup), 1)[0]);
                message = 'Moved ' + sourceGroup.name;
            } else moveItem(result.move.fromId, result.move.toId);
        }
    }, { color: ui.colors.surface });
}

/** Rich text: spans with their own size and color, wrapped together. */
function notesFor(item) {
    return [
        { text: item.name, size: ui.sp(18), color: ui.colors.accent },
        ' collects ' + item.type.toLowerCase() + ' views. Drag rows in the library to reorder them, and ',
        { text: 'every change animates in place', color: ui.colors.ink },
        '. This note wraps to the width of the inspector and is measured before layout, so the rows below ' +
        'always make room for it.',
    ];
}

/** A preview card: skeleton lines while "loading", then a gradient card. */
function previewCard(rect, item) {
    const now = ui.input.totalTime || 0;
    if (preview.id !== item.id) preview = { id: item.id, since: now };
    if (now - preview.since < 1.2) {
        const [line1, line2] = ui.rows(ui.inset(rect, dp(16)), [dp(18), dp(14)], dp(10));
        ui.outline(rect, ui.colors.border, 1, dp(12));
        ui.skeleton({ ...line1, width: line1.width * 0.45 }, { radius: dp(6) });
        ui.skeleton({ ...line2, width: line2.width * 0.3 }, { radius: dp(6) });
        return;
    }
    ui.presence('preview:' + item.id, true, appear => ui.layer(rect, { opacity: appear, y: (1 - appear) * dp(8) }, () => {
        if (typeof sys.canvas.setGradient === 'function') {
            sys.canvas.setFillColor(ui.colors.accent);
            sys.canvas.setGradient('linear', rect.x, rect.y, rect.x + rect.width, rect.y + rect.height,
                [ui.colors.accent, withAlpha(ui.colors.highlight, 0.9)]);
            sys.canvas.drawRoundRect(rect.x, rect.y, rect.width, rect.height, dp(12), dp(12));
            sys.canvas.setGradient(null);
        } else ui.fill(rect, ui.colors.accent, dp(12));
        const size = dp(20);
        ui.text(item.name, rect.x + dp(16), rect.y + dp(30), size, ui.colors.accentInk);
        ui.text(Math.round(item.level * 100) + '% intensity', rect.x + dp(16), rect.y + dp(54), dp(15),
            withAlpha(ui.colors.accentInk, 0.8));
    }));
}

function inspector(rect) {
    ui.panel(rect, inner => {
        const selected = selectedItem();
        const notes = notesFor(selected);
        const notesHeight = ui.measureRichText(notes, inner.width - dp(14), { size: ui.sp(16) }).height;
        const sizes = [51, 34, 0, 76, 30, 48, 30, 48, 62, 52, 52, 30, 46, 50, 43].map(dp);
        sizes[2] = notesHeight;
        const contentHeight = sizes.reduce((sum, size) => sum + size, 0) + dp(8) * (sizes.length - 1);
        ui.scroll('inspector', inner, contentHeight, content => {
            const [header, description, notesRect, previewRect, nameLabel, nameField, typeLabel, typeRow, toggleRow,
                crtRow, motionRow, meterLabel, meterRow, buttons, hint] = ui.rows(content, sizes, dp(8));
            ui.richText(notes, notesRect, { size: ui.sp(16), color: ui.colors.muted });
            previewCard(previewRect, selected);
            const renamed = ui.inlineEdit('name:' + selected.id, selected.name, header, { maxLength: 48 });
            if (renamed !== null) {
                selected.name = renamed;
                message = 'Renamed view to ' + renamed;
            }
            ui.label(selected.type + '  /  ' + selected.id.toUpperCase(), description,
                { size: dp(16), color: ui.colors.muted });
            ui.label('Workspace title', nameLabel, { size: dp(18) });
            ui.field('title', nameField, title, { placeholder: 'Untitled workspace', label: 'Workspace title', maxLength: 48 });
            ui.label('Category', typeLabel, { size: dp(18) });
            selected.type = ui.select('type:' + selected.id, typeRow,
                ['Overview', 'Analytics', 'Preferences', 'Workspace', 'Library', 'Schedule', 'Messages', 'Media'],
                selected.type, { label: 'Category' });
            selected.enabled = ui.toggle('enabled:' + selected.id, 'Active', toggleRow, selected.enabled);
            crtEnabled = ui.checkbox('crt', 'CRT effect', crtRow, crtEnabled);
            ui.reducedMotion = ui.toggle('reduced-motion', 'Reduce motion', motionRow, ui.reducedMotion);
            ui.label('Intensity  /  ' + Math.round(selected.level * 100) + '%', meterLabel, { size: dp(18) });
            selected.level = ui.slider('level:' + selected.id, meterRow, selected.level, { label: 'Intensity' });
            const [apply, reset] = ui.columns(buttons, [{ weight: 1 }, { weight: 1 }], dp(10));
            if (ui.button('apply', 'Apply', apply, { primary: true })) {
                message = 'Applied ' + title.value + ' to ' + selected.name;
                toast = { text: 'Saved ' + selected.name, until: (ui.input.totalTime || 0) + 2.5 };
            }
            if (ui.button('reset', 'Reset', reset)) {
                selected.level = 0.5;
                selected.enabled = true;
                message = 'Reset ' + selected.name;
            }
            ui.label(message, hint, { color: ui.colors.accent, size: dp(17) });
        });
    });
}

function header(rect, narrow) {
    const [titleRect, modeRect, themeRect] = ui.columns(rect,
        [{ weight: 1 }, Math.min(dp(250), rect.width * 0.36), Math.min(dp(170), rect.width * 0.3)], dp(10));
    ui.label(narrow ? 'BUDO' : 'BUDO  /  UI', titleRect, { size: dp(narrow ? 23 : 30) });
    mode = ui.segmented('mode', { ...modeRect, y: modeRect.y + dp(12), height: dp(40) }, ['Workspace', 'Gallery'], mode,
        { label: 'View' });
    const picked = ui.select('theme', { ...themeRect, y: themeRect.y + dp(8), height: dp(44) },
        ['System', 'Light', 'Dark'], themeName, { label: 'Theme' });
    if (picked !== themeName) {
        themeName = picked;
        ui.setTheme(picked === 'System' ? 'system' : picked === 'Dark' ? themes.dark : themes.light);
    }
}

function toastLayer(page) {
    const now = ui.input.totalTime || 0;
    const visible = toast !== null && now < toast.until;
    const width = Math.min(dp(320), page.width);
    const rect = { x: page.x + (page.width - width) / 2, y: page.y + page.height - dp(64), width, height: dp(48) };
    const shown = toast;
    ui.overlay(() => ui.presence('toast', visible, appear => {
        ui.layer(rect, {
            opacity: appear, y: (1 - appear) * dp(24), scale: 0.96 + 0.04 * appear,
            backdrop: dp(16), radius: dp(24),
        }, () => {
            ui.fill(rect, withAlpha(ui.colors.ink, 0.82), dp(24));
            const size = dp(18);
            const text = shown ? shown.text : '';
            ui.text(text, rect.x + (rect.width - sys.canvas.measureText(text, size)) / 2,
                rect.y + rect.height / 2 + size * 0.35, size, ui.colors.surface);
        });
    }));
    if (!visible && toast !== null && now >= toast.until + 1) toast = null;
    // Nothing moves while the toast is shown: wake up when it should leave.
    if (visible) ui.wakeAfter(toast.until - now);
}

function frame() {
    const input = sys.input.get();
    ui.begin(input);
    ui.clear();
    const { width } = ui.bounds;

    const margin = dp(width < 600 ? 12 : 22);
    const page = ui.inset(ui.bounds, margin);
    const [headerRect, workspace, footer] = ui.rows(page, [dp(72), { weight: 1 }, dp(38)], dp(10));
    header(headerRect, width < 600);
    const compact = width < dp(740);
    if (mode === 'Gallery') drawGallery(ui, workspace, gallery);
    else ui.split('workspace', workspace, sidebar, inspector,
        { vertical: compact, ratio: compact ? 0.43 : 0.33, min: 0.24, max: 0.72 });
    const selected = selectedItem();
    const meter = ui.spring('meter', selected.level);
    ui.fill({ x: footer.x, y: footer.y + dp(12), width: footer.width, height: dp(3) }, ui.colors.border, dp(1));
    ui.fill({ x: footer.x, y: footer.y + dp(12), width: footer.width * meter, height: dp(3) }, ui.colors.accent, dp(1));
    ui.label(selected.name + '  /  ' + (selected.enabled ? 'ACTIVE' : 'INACTIVE'),
        { x: footer.x, y: footer.y + dp(18), width: footer.width, height: dp(20) },
        { size: dp(15), color: ui.colors.muted });
    toastLayer(page);
    ui.end();
    if (crtEnabled && crtProgram > 0) {
        sys.gl.setUniform1f(crtProgram, 'u_scanline_intensity', 0.35);
        sys.gl.setUniform1f(crtProgram, 'u_aberration', 0.5);
        sys.gl.bindScreen();
        sys.gl.drawFullscreen(crtProgram);
        // The shader draws straight to the screen, so it needs every frame.
        sys.animation.requestFrame(frame);
    } else ui.nextFrame(frame); // idle: draw again only on input
}

sys.animation.requestFrame(frame);