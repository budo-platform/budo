/** @typedef {{ name: string, kind: string }} PickerRow */

/** @param {string} path */
function parentPath(path) {
    return path.slice(0, Math.max(0, path.lastIndexOf('/')));
}

/** @param {string} input */
function mountPath(input) {
    const value = input.trim();
    if (!value || value.includes('\0')) throw new Error('Enter a mounted file path.');
    if (value === 'files' || value === 'assets' || value.startsWith('files/') || value.startsWith('assets/')) return value;
    if (value.startsWith('/')) throw new Error('Use a path relative to the file root.');
    return 'files/' + value;
}

export class FilePicker {
    error = '';
    /** @type {PickerRow[]} */
    rows = [];
    /** @type {PickerRow | null} */
    selected = null;
    reset = true;

    /** @param {boolean} saving @param {string} from @param {string} [directory] */
    constructor(saving, from, directory = parentPath(from)) {
        this.saving = saving;
        this.path = { value: saving ? from.slice(from.lastIndexOf('/') + 1) : '' };
        this.directory = '';
        this.browse(saving && directory !== 'files' && !directory.startsWith('files/') ? 'files' : directory);
    }

    /** @param {string} directory */
    browse(directory) {
        const entries = directory ? sys.files.list(directory) : sys.files.list();
        if (!entries) throw new Error(sys.files.getError() || 'Cannot list this directory');
        const rows = entries.map(entry => ({ name: entry.name, kind: entry.type === 'directory' ? 'Folder' : 'File' }));
        rows.sort((a, b) => Number(a.kind === 'File') - Number(b.kind === 'File') || a.name.localeCompare(b.name));
        Object.assign(this, { directory, rows, selected: null, reset: true, error: '' });
    }

    /** @param {any} ui @param {import('./editor-ui/lib/layout.js').Rect} content
     *  @param {{ browse: (directory: string) => void, open: (path: string) => void,
     *  save: (path: string) => void, close: () => void }} actions */
    draw(ui, content, actions) {
        const dp = ui.dp, weight = { weight: 1 };
        const [location, table, path, message, buttons] = ui.rows(content,
            [dp(32), weight, dp(34), dp(21), dp(36)], dp(5));
        const [up, folder] = ui.columns(location, [dp(48), weight], dp(8));
        const canGoUp = !!this.directory && (!this.saving || this.directory !== 'files');
        if (ui.button('picker-up', 'Up', up, { disabled: !canGoUp }))
            return () => actions.browse(parentPath(this.directory));
        ui.paragraph(this.directory ? this.directory + '/' : 'Mounted locations', folder, { maxLines: 1 });

        const result = ui.table('picker-files', table, {
            label: 'Directory contents', emptyText: 'No entries', reset: this.reset, rowHeight: dp(30),
            columns: [{ key: 'name', label: 'Name' }, { key: 'kind', label: 'Type', width: 70 }],
            rowCount: this.rows.length, row: i => this.rows[i], rowId: i => this.rows[i].name,
            selectedId: this.selected?.name ?? null,
        });
        this.reset = false;
        if (result.selectedId !== (this.selected?.name ?? null)) {
            this.selected = this.rows.find(row => row.name === result.selectedId) ?? null;
            if (this.saving && this.selected?.kind === 'File') this.path.value = this.selected.name;
            if (!this.saving) this.path.value = '';
            this.error = '';
        }
        const selected = this.selected;

        ui.field('picker-path', path, this.path, {
            label: this.saving ? 'Filename or mounted path' : 'File path',
            placeholder: this.saving ? 'Filename or files/path' : 'Type a path, e.g. files/notes.txt',
            maxLength: 1024,
        });
        ui.paragraph(this.error || (this.saving ? 'Choose a folder or type a path.' : 'Browse folders or type a path relative to files/.'),
            message, { maxLines: 1 });
        const [folderAction, accept, cancel] = ui.columns(buttons, this.saving ? [weight, weight, weight] : [weight, weight], dp(6));
        /** @param {PickerRow} row */
        const choose = row => {
            const path = this.directory ? this.directory + '/' + row.name : row.name;
            if (row.kind === 'Folder') actions.browse(path);
            else if (!this.saving) actions.open(path);
        };
        if (this.saving && ui.button('picker-cancel', 'Cancel', cancel)) return actions.close;
        if (!this.saving && ui.button('picker-new', 'New', folderAction)) return () => actions.open('');
        if (this.saving && selected?.kind === 'Folder' && ui.button('picker-folder', 'Enter', folderAction))
            return () => choose(selected);

        const typedPath = this.path.value;
        if (ui.button('picker-accept', this.saving ? 'Save' : 'Open', accept,
            { primary: true, disabled: !this.saving && !selected && !typedPath.trim() })) {
            if (!this.saving) {
                if (!typedPath.trim()) return selected ? () => choose(selected) : null;
                return () => {
                    const target = mountPath(typedPath).replace(/\/+$/, '');
                    if (sys.files.isDirectory(target)) {
                        actions.browse(target);
                        this.path.value = '';
                    } else actions.open(target);
                };
            }
            return () => {
                if (!typedPath.trim() || typedPath === '.' || typedPath === '..' || typedPath.includes('\0'))
                    throw new Error('Enter a filename or mounted save path.');
                const target = typedPath.includes('/') ? mountPath(typedPath) : this.directory + '/' + typedPath;
                actions.save(target);
            };
        }
        if (result.activatedId !== null && selected) return () => choose(selected);
        return null;
    }
}
