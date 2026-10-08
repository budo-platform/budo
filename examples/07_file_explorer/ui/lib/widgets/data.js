/** Data views: a table and a tree. Both are virtualized (only the visible rows
 *  are laid out and drawn, so thousands of rows cost a screenful) and keyboard
 *  driven: the arrows move the selection, Enter activates, and the selection
 *  scrolls into view. The app owns the rows and the selection. */

import { clamp } from '../math.js';
import { springs } from '../motion.js';
import { withAlpha } from '../color.js';

/** @typedef {import('../layout.js').Rect} Rect */
/** @typedef {{ key: string, label: string, width?: number | { weight: number }, align?: 'left' | 'right',
 *   sortable?: boolean, minWidth?: number }} TableColumn */
/** @typedef {{ columns: TableColumn[], rowCount: number, row: (index: number) => Record<string, any>,
 *   rowId?: (index: number) => string, selectedId?: string | null, sort?: { key: string, descending?: boolean } | null,
 *   rowHeight?: number, label?: string, emptyText?: string,
 *   cell?: (column: TableColumn, row: Record<string, any>, rect: Rect, selected: boolean) => boolean | void }} TableOptions */
/** @typedef {{ selectedId: string | null, activatedId: string | null,
 *   sort: { key: string, descending: boolean } | null }} TableResult */
/** @typedef {{ id: string, label: string, children?: TreeNode[], icon?: string, expanded?: boolean }} TreeNode */
/** @typedef {{ selectedId?: string | null, rowHeight?: number, indent?: number, label?: string }} TreeOptions */
/** @typedef {{ selectedId: string | null, activatedId: string | null, toggledId: string | null }} TreeResult */

const KEY_ENTER = 40, KEY_PAGE_UP = 75, KEY_HOME = 74, KEY_END = 77, KEY_PAGE_DOWN = 78,
    KEY_RIGHT = 79, KEY_LEFT = 80, KEY_DOWN = 81, KEY_UP = 82;

/** @param {any} ui @param {any} ctx */
export function install(ui, ctx) {
    const t = () => ctx.theme;
    /** @type {Map<string, Record<string, number>>} column widths the user dragged, by table */
    const widths = new Map();
    /** @type {Map<string, { key: string, startX: number, startWidth: number }>} column being resized */
    const resizing = new Map();
    /** @type {Map<string, Set<string>>} expanded node ids, by tree */
    const expansions = new Map();
    /** @type {Map<string, { id: string, time: number }>} last row click, for double clicks */
    const lastClicks = new Map();

    /** The current (shown) scroll offset of the scroll area `scrollId`. */
    const offsetOf = scrollKey => ctx.motion.get('n:' + scrollKey + '#offset')?.spring.value || 0;

    /** Keyboard moves through `count` rows: returns the new index, or null. */
    function keyboardIndex(index, count, page) {
        const step = key => ctx.keyRepeated(key);
        if (step(KEY_DOWN)) return clamp(index < 0 ? 0 : index + 1, 0, count - 1);
        if (step(KEY_UP)) return clamp(index < 0 ? count - 1 : index - 1, 0, count - 1);
        if (step(KEY_PAGE_DOWN)) return clamp(Math.max(0, index) + page, 0, count - 1);
        if (step(KEY_PAGE_UP)) return clamp(Math.max(0, index) - page, 0, count - 1);
        if (ctx.keyPressed(KEY_HOME)) return 0;
        if (ctx.keyPressed(KEY_END)) return count - 1;
        return null;
    }

    /** Scroll so that row `index` shows, before the scroll area is drawn this frame. */
    function revealRow(scrollKey, view, index, rowHeight) {
        const y = view.y - offsetOf(scrollKey) + index * rowHeight;
        ctx.reveal = { scroll: scrollKey, rect: ctx.toScreen({ x: view.x, y, width: view.width, height: rowHeight }) };
    }

    /** A click on row `id`: true when it is the second within 0.4 s. */
    function doubleClicked(key, id) {
        const now = ctx.input.totalTime || 0;
        const last = lastClicks.get(key);
        lastClicks.set(key, { id, time: now });
        return !!last && last.id === id && now - last.time < 0.4;
    }

    /** Draw the visible rows of a virtual list inside a scroll area.
     *  @param {(index: number, rect: Rect) => void} drawRow */
    function virtualRows(scrollId, view, count, rowHeight, drawRow) {
        ui.scroll(scrollId, view, count * rowHeight, content => {
            const first = clamp(Math.floor((view.y - content.y) / rowHeight), 0, Math.max(0, count - 1));
            const last = clamp(Math.ceil((view.y + view.height - content.y) / rowHeight), 0, count - 1);
            for (let index = first; index <= last && count > 0; index++)
                drawRow(index, { x: content.x, y: content.y + index * rowHeight, width: content.width, height: rowHeight });
        }, { radius: 0 });
    }

    // ── Table ──────────────────────────────────────────────────────────────

    /** A virtualized table with a header. Click a sortable header to sort (the
     *  app sorts its rows from `result.sort`); drag a header edge to resize.
     *  @param {string} id @param {Rect} rect @param {TableOptions} options @returns {TableResult} */
    function table(id, rect, options) {
        const theme = t(), px = ctx.px, input = ctx.input;
        const key = ctx.fullId(id);
        const rowHeight = options.rowHeight || px(40);
        const headerHeight = px(40);
        const size = ctx.sp(theme.small || theme.font);
        const count = Math.max(0, options.rowCount | 0);
        const rowId = options.rowId || (index => String(index));
        const result = { selectedId: options.selectedId ?? null, activatedId: null, sort: null };
        const container = ctx.interact(id, rect, {
            cursor: null, a11y: { role: 'list', label: options.label || 'Table' },
        });
        ctx.fill(rect, theme.surface);
        ctx.outline(rect, theme.border, 1);
        const inner = ui.inset(rect, px(1));
        const header = { ...inner, height: headerHeight };
        const view = { ...inner, y: inner.y + headerHeight, height: Math.max(0, inner.height - headerHeight) };
        const scrollId = id + ':rows';
        const scrollKey = ctx.fullId(scrollId);

        // Column widths: declared sizes, then what the user dragged.
        const dragged = widths.get(key) || {};
        const gutter = count * rowHeight > view.height ? px(12) : 0;
        const sizes = options.columns.map(column => dragged[column.key] !== undefined ? dragged[column.key]
            : typeof column.width === 'number' ? px(column.width) : column.width || { weight: 1 });
        const cells = ui.columns({ ...header, width: header.width - gutter }, sizes, 0);

        // Keyboard: the table has the focus (Tab, or a click on a row). Only
        // then is the selected row looked up, so idle tables stay O(visible).
        if (container.focused && count > 0) {
            let selectedIndex = -1;
            if (result.selectedId !== null)
                for (let index = 0; index < count && selectedIndex < 0; index++)
                    if (rowId(index) === result.selectedId) selectedIndex = index;
            ctx.arrowsTaken = true;
            const next = keyboardIndex(selectedIndex, count, Math.max(1, Math.floor(view.height / rowHeight) - 1));
            if (next !== null && next !== selectedIndex) {
                result.selectedId = rowId(next);
                revealRow(scrollKey, view, next, rowHeight);
            }
            if (selectedIndex >= 0 && ctx.keyPressed(KEY_ENTER)) result.activatedId = result.selectedId;
        }

        // Header: labels, sort arrows, resize handles.
        ctx.fill(header, theme.raised, 0);
        options.columns.forEach((column, index) => {
            const cell = cells[index];
            const sorted = options.sort?.key === column.key;
            if (column.sortable) {
                const state = ctx.interact(id + ':sort:' + column.key, cell, { focusable: false });
                if (state.hovered) ctx.fill(cell, withAlpha(theme.border, 0.45), 0);
                if (state.clicked)
                    result.sort = { key: column.key, descending: sorted ? !options.sort?.descending : false };
            }
            const label = ctx.truncate(column.label, cell.width - px(sorted ? 36 : 20), size);
            const labelWidth = ctx.measure(label, size);
            const x = column.align === 'right' ? cell.x + cell.width - px(10) - labelWidth - (sorted ? px(18) : 0) : cell.x + px(10);
            ctx.text(label, x, cell.y + cell.height / 2 + size * 0.35, size, sorted ? theme.ink : theme.muted);
            if (sorted) ctx.icon(options.sort?.descending ? 'chevronDown' : 'chevronUp',
                { x: x + labelWidth + px(4), y: cell.y + (cell.height - px(14)) / 2, width: px(14), height: px(14) },
                { color: theme.ink });
            if (index < options.columns.length - 1) {
                const handle = { x: cell.x + cell.width - px(4), y: cell.y, width: px(8), height: cell.height };
                const grip = ctx.interact(id + ':resize:' + column.key, handle, { cursor: 'ew-resize', focusable: false });
                if (input.pointer.pressed && grip.hovered)
                    resizing.set(key, { key: column.key, startX: input.pointer.x, startWidth: cell.width });
                const drag = resizing.get(key);
                if (drag && drag.key === column.key) {
                    if (input.pointer.down) {
                        const minimum = px(column.minWidth || 48);
                        widths.set(key, { ...dragged, [column.key]: Math.max(minimum, drag.startWidth + input.pointer.x - drag.startX) });
                        ctx.cursor = 'ew-resize';
                    } else resizing.delete(key);
                }
                ctx.line(cell.x + cell.width, cell.y + px(10), cell.x + cell.width, cell.y + cell.height - px(10),
                    grip.hovered || drag?.key === column.key ? theme.accent : theme.border);
            }
        });
        ctx.line(inner.x, header.y + header.height, inner.x + inner.width, header.y + header.height, theme.border);

        if (count === 0 && options.emptyText)
            ctx.text(options.emptyText, view.x + px(12), view.y + px(28), size, theme.muted);

        virtualRows(scrollId, view, count, rowHeight, (index, rowRect) => {
            const row = options.row(index);
            const idOfRow = rowId(index);
            const selected = idOfRow === result.selectedId;
            const label = options.columns.map(column => String(row[column.key] ?? '')).join(', ');
            const state = ctx.interact(id + ':row:' + idOfRow, rowRect, {
                focusable: false, a11y: { role: 'listitem', label, selected },
            });
            if (state.clicked) {
                result.selectedId = idOfRow;
                if (state.action?.action !== 'press' && doubleClicked(key, idOfRow)) result.activatedId = idOfRow;
                ctx.keyFocus = container.id;
            }
            const background = selected ? withAlpha(theme.accent, container.focused ? 0.2 : 0.12)
                : state.hovered ? theme.raised : withAlpha(theme.raised, 0);
            ctx.fill(rowRect, ctx.colorAt(state.id + '#bg', background), 0);
            options.columns.forEach((column, columnIndex) => {
                const cell = { ...cells[columnIndex], y: rowRect.y, height: rowRect.height };
                if (options.cell && options.cell(column, row, cell, selected)) return;
                const text = ctx.truncate(String(row[column.key] ?? ''), cell.width - px(20), size);
                const x = column.align === 'right' ? cell.x + cell.width - px(10) - ctx.measure(text, size) : cell.x + px(10);
                ctx.text(text, x, cell.y + cell.height / 2 + size * 0.35, size, theme.ink);
            });
            ctx.line(rowRect.x, rowRect.y + rowRect.height, rowRect.x + rowRect.width, rowRect.y + rowRect.height,
                withAlpha(theme.border, 0.6));
        });
        return result;
    }

    // ── Tree ───────────────────────────────────────────────────────────────

    /** A virtualized tree. Click a chevron (or press Right / Left) to expand or
     *  collapse; the tree remembers what is expanded (start with `expanded`).
     *  @param {string} id @param {Rect} rect @param {TreeNode[]} nodes
     *  @param {TreeOptions} [options] @returns {TreeResult} */
    function tree(id, rect, nodes, options = {}) {
        const theme = t(), px = ctx.px;
        const key = ctx.fullId(id);
        const rowHeight = options.rowHeight || px(36);
        const indent = options.indent === undefined ? px(20) : options.indent;
        const size = ctx.sp(theme.font);
        const result = { selectedId: options.selectedId ?? null, activatedId: null, toggledId: null };
        let expanded = expansions.get(key);
        if (!expanded) {
            expanded = new Set();
            const seed = list => list.forEach(node => {
                if (node.expanded) expanded.add(node.id);
                if (node.children) seed(node.children);
            });
            seed(nodes);
            expansions.set(key, expanded);
        }
        // The visible rows: nodes under expanded parents, with depth and parent.
        /** @type {{ node: TreeNode, depth: number, parent: string | null }[]} */
        const rows = [];
        const flatten = (list, depth, parent) => {
            for (const node of list) {
                rows.push({ node, depth, parent });
                if (node.children?.length && expanded.has(node.id)) flatten(node.children, depth + 1, node.id);
            }
        };
        flatten(nodes, 0, null);

        const container = ctx.interact(id, rect, { cursor: null, a11y: { role: 'list', label: options.label || 'Tree' } });
        const view = ui.inset(rect, px(4));
        const scrollId = id + ':rows';
        const scrollKey = ctx.fullId(scrollId);
        const toggle = nodeId => {
            if (expanded.has(nodeId)) expanded.delete(nodeId);
            else expanded.add(nodeId);
            result.toggledId = nodeId;
            ctx.wake = 0;
        };

        let selectedIndex = rows.findIndex(row => row.node.id === result.selectedId);
        if (container.focused && rows.length) {
            ctx.arrowsTaken = true;
            const current = selectedIndex >= 0 ? rows[selectedIndex] : null;
            let next = keyboardIndex(selectedIndex, rows.length, Math.max(1, Math.floor(view.height / rowHeight) - 1));
            if (next === null && current && ctx.keyRepeated(KEY_RIGHT)) {
                if (current.node.children?.length && !expanded.has(current.node.id)) toggle(current.node.id);
                else if (current.node.children?.length) next = selectedIndex + 1;
            } else if (next === null && current && ctx.keyRepeated(KEY_LEFT)) {
                if (current.node.children?.length && expanded.has(current.node.id)) toggle(current.node.id);
                else if (current.parent !== null) next = rows.findIndex(row => row.node.id === current.parent);
            }
            if (next !== null && next >= 0 && next !== selectedIndex) {
                result.selectedId = rows[next].node.id;
                selectedIndex = next;
                revealRow(scrollKey, view, next, rowHeight);
            }
            if (current && ctx.keyPressed(KEY_ENTER)) result.activatedId = current.node.id;
        }

        ctx.fill(rect, theme.surface);
        virtualRows(scrollId, view, rows.length, rowHeight, (index, rowRect) => {
            const { node, depth } = rows[index];
            const selected = node.id === result.selectedId;
            const parent = !!node.children?.length;
            const open = parent && expanded.has(node.id);
            const state = ctx.interact(id + ':node:' + node.id, rowRect, {
                focusable: false,
                a11y: { role: 'listitem', label: node.label, selected, expanded: parent ? open : undefined },
            });
            const x = rowRect.x + px(8) + depth * indent;
            const chevron = { x, y: rowRect.y + (rowRect.height - px(18)) / 2, width: px(18), height: px(18) };
            const onChevron = parent && ctx.inside({ ...chevron, x: chevron.x - px(6), width: chevron.width + px(12) });
            if (state.clicked) {
                if (onChevron || state.action?.action === 'press' && parent) toggle(node.id);
                else {
                    result.selectedId = node.id;
                    if (state.action?.action !== 'press' && doubleClicked(key, node.id)) {
                        if (parent) toggle(node.id);
                        result.activatedId = node.id;
                    }
                }
                ctx.keyFocus = container.id;
            }
            const background = selected ? withAlpha(theme.accent, container.focused ? 0.2 : 0.12)
                : state.hovered ? theme.raised : withAlpha(theme.raised, 0);
            ctx.fill(rowRect, ctx.colorAt(state.id + '#bg', background), px(theme.radius));
            if (parent) {
                // The chevron turns as the node opens.
                const turn = ctx.springAt(state.id + '#turn', open ? 90 : 0, springs.snappy);
                const cx = chevron.x + chevron.width / 2, cy = chevron.y + chevron.height / 2;
                sys.canvas.save();
                sys.canvas.translate(cx, cy);
                sys.canvas.rotate?.(turn);
                sys.canvas.translate(-cx, -cy);
                ctx.icon('chevronRight', chevron, { color: theme.muted });
                sys.canvas.restore();
            }
            let textX = chevron.x + chevron.width + px(6);
            if (node.icon) {
                ctx.icon(node.icon, { x: textX, y: rowRect.y + (rowRect.height - px(18)) / 2, width: px(18), height: px(18) },
                    { color: selected ? theme.accent : theme.muted });
                textX += px(24);
            }
            const text = ctx.truncate(node.label, rowRect.x + rowRect.width - textX - px(8), size);
            ctx.text(text, textX, rowRect.y + rowRect.height / 2 + size * 0.35, size, theme.ink);
        });
        return result;
    }

    Object.assign(ui, { table, tree });
}
