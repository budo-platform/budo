import { createUI, Smoothed } from './ui-library.js';

const ui = createUI();
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
const meter = new Smoothed(0, 0.2);

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
        const [heading, filter, list] = ui.rows(inner, [38, 48, { weight: 1 }], 10);
        const count = groups.reduce((sum, group) => sum + group.items.length, 0);
        ui.label('LIBRARY  /  ' + count, heading, { size: 19, color: ui.colors.muted });
        ui.field('search', filter, search, { placeholder: 'Filter views' });
        const query = search.value.toLowerCase();
        const visible = groups.flatMap(group => {
            const children = group.items.filter(item => group.name.toLowerCase().includes(query) ||
                item.name.toLowerCase().includes(query));
            if (!children.length && !group.name.toLowerCase().includes(query)) return [];
            return [
                {
                    id: 'group:' + group.id, title: group.name, kind: 'group', height: 50,
                    action: true, actionLabel: '+'
                },
                ...children.map(item => ({
                    id: item.id, title: item.name, subtitle: item.type,
                    kind: 'item', groupId: group.id, depth: 1, height: 60,
                    action: true, actionLabel: '-'
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

function inspector(rect) {
    ui.panel(rect, inner => {
        const selected = selectedItem();
        ui.scroll('inspector', inner, 518, content => {
            const [header, description, nameLabel, nameField, toggleRow, crtRow, meterLabel,
                meterRow, buttons, hint] = ui.rows(content,
                    [51, 34, 30, 48, 62, 52, 30, 46, 50, 43], 8);
            const renamed = ui.inlineEdit('name:' + selected.id, selected.name, header, { maxLength: 48 });
            if (renamed !== null) {
                selected.name = renamed;
                message = 'Renamed view to ' + renamed;
            }
            ui.label(selected.type + '  /  ' + selected.id.toUpperCase(), description,
                { size: 16, color: ui.colors.muted });
            ui.label('Workspace title', nameLabel, { size: 18 });
            ui.field('title', nameField, title, { placeholder: 'Untitled workspace', maxLength: 48 });
            selected.enabled = ui.toggle('enabled:' + selected.id, 'Active', toggleRow, selected.enabled);
            crtEnabled = ui.checkbox('crt', 'CRT effect', crtRow, crtEnabled);
            ui.label('Intensity  /  ' + Math.round(selected.level * 100) + '%', meterLabel, { size: 18 });
            selected.level = ui.slider('level:' + selected.id, meterRow, selected.level);
            const [apply, reset] = ui.columns(buttons, [{ weight: 1 }, { weight: 1 }], 10);
            if (ui.button('apply', 'Apply', apply, { primary: true }))
                message = 'Applied ' + title.value + ' to ' + selected.name;
            if (ui.button('reset', 'Reset', reset)) {
                selected.level = 0.5;
                selected.enabled = true;
                message = 'Reset ' + selected.name;
            }
            ui.label(message, hint, { color: ui.colors.accent, size: 17 });
        });
    });
}

function frame() {
    const width = sys.window.getWidth();
    const height = sys.window.getHeight();
    const bounds = { x: 0, y: 0, width, height };
    const input = sys.input.get();
    sys.canvas.clear(ui.colors.background);
    ui.begin(input, bounds);

    const margin = width < 600 ? 12 : 22;
    const page = ui.inset(bounds, margin);
    const [header, workspace, footer] = ui.rows(page, [72, { weight: 1 }, 38], 10);
    ui.label('BUDO  /  UI LIBRARY', header, { size: width < 600 ? 23 : 30 });
    const compact = width < 740;
    ui.split('workspace', workspace, sidebar, inspector,
        { vertical: compact, ratio: compact ? 0.43 : 0.33, min: 0.24, max: 0.72 });
    const selected = selectedItem();
    meter.target = selected.level;
    meter.update(Math.min(input.deltaTime || 0.016, 0.1));
    ui.fill({ x: footer.x, y: footer.y + 12, width: footer.width, height: 3 }, ui.colors.border, 1);
    ui.fill({ x: footer.x, y: footer.y + 12, width: footer.width * meter.value, height: 3 }, ui.colors.accent, 1);
    ui.label(selected.name + '  /  ' + (selected.enabled ? 'ACTIVE' : 'INACTIVE'),
        { x: footer.x, y: footer.y + 18, width: footer.width, height: 20 },
        { size: 15, color: ui.colors.muted });
    ui.end();
    if (crtEnabled && crtProgram > 0) {
        sys.gl.setUniform1f(crtProgram, 'u_scanline_intensity', 0.35);
        sys.gl.setUniform1f(crtProgram, 'u_aberration', 0.5);
        sys.gl.bindScreen();
        sys.gl.drawFullscreen(crtProgram);
    }
    sys.animation.requestFrame(frame);
}

sys.animation.requestFrame(frame);