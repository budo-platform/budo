import { Document } from './document.js';
import { Page, pageBytes } from './page.js';

const blockSize = 1024 * 1024;
const fallbackLimit = 8 * blockSize;

/** @param {string} fallback */
function fileError(fallback) {
    return new Error(sys.files.getError() || fallback);
}

/** The active document, its one editable page, and the staged save that locks them. */
export class Workspace {
    #document = new Document();
    #revision = 0;
    #hexColumns = 16;
    #hexCapacity = pageBytes.Hex;
    #page = new Page(this.#document, 'Text', 0, this.#revision);
    /** @type {{ path: string, handle: number, position: number } | null} */
    #save = null;

    get path() { return this.#document.path; }
    get size() { return this.#document.size; }
    get page() { return this.#page; }
    get hexColumns() { return this.#hexColumns; }
    get hexCapacity() { return this.#hexCapacity; }
    get dirty() { return this.#document.dirty || this.#page.pending; }
    get saving() { return this.#save !== null; }
    get savePath() { return this.#save?.path ?? ''; }
    get saveProgress() { return this.#save && this.size ? this.#save.position / this.size : 1; }

    #requireIdle() {
        if (this.#save) throw new Error('Wait for the current save or cancel it first.');
    }

    /** @param {string} path */
    open(path) {
        this.#requireIdle();
        const next = new Document(path);
        let page;
        try {
            page = new Page(next, this.page.mode, 0, this.#revision + 1, this.#hexColumns,
                this.page.mode === 'Hex' ? this.#hexCapacity : pageBytes.Text);
        } catch (error) {
            next.close();
            throw error;
        }
        this.#setDocument(next);
        this.#page = page;
        this.#revision++;
    }

    /** @param {Document} next */
    #setDocument(next) {
        this.#document.close();
        this.#document = next;
    }

    /** @param {number} offset @param {import('./page.js').Mode} mode */
    navigate(offset, mode = this.page.mode) {
        this.#requireIdle();
        this.page.commit(this.#document);
        this.#loadPage(offset, mode);
    }

    previous() {
        this.navigate(Math.max(0, this.page.start - (this.page.mode === 'Hex' ? this.#hexCapacity : pageBytes.Text)));
    }

    next() {
        this.#requireIdle();
        this.page.commit(this.#document);
        this.#loadPage(this.page.end, this.page.mode);
    }

    /** @param {string} input */
    go(input) {
        if (!/^(?:\d+|0x[0-9a-f]+)$/i.test(input))
            throw new Error('Enter a decimal or 0x hexadecimal byte offset.');
        const offset = Number(input);
        if (!Number.isSafeInteger(offset)) throw new Error('Offset is outside the document.');
        return this.seekViewport(offset);
    }

    setHexLayout(columns, capacity, offset) {
        const index = this.#seekViewport(offset, 'Hex', 0, columns, capacity);
        this.#hexColumns = columns;
        this.#hexCapacity = capacity;
        return index;
    }

    seekViewport(offset, mode = this.page.mode, direction = 0) {
        return this.#seekViewport(offset, mode, direction, this.#hexColumns,
            mode === 'Hex' ? this.#hexCapacity : pageBytes.Text);
    }

    /** Move the buffered window around an absolute byte, with room to scroll in both directions.
     *  @param {number} offset @param {import('./page.js').Mode} mode @param {number} direction */
    #seekViewport(offset, mode, direction, hexColumns, capacity) {
        const old = this.page;
        this.#requireIdle();
        this.page.commit(this.#document);
        if (offset > this.size) throw new Error('Offset is outside the document.');
        const tail = Math.max(0, this.size - capacity);
        let start = Math.max(0, Math.min(offset - capacity / 2, tail));
        if (direction > 0 && start <= old.start) start = Math.min(offset, tail);
        if (mode === 'Hex') {
            start = start === tail ? Math.min(offset, Math.ceil(start / hexColumns) * hexColumns) : start;
            start -= start % hexColumns;
        }
        else if (start > 0) {
            // Start on a nearby line boundary when possible, without scanning long lines.
            const prefix = this.#document.read(start, Math.min(256, offset - start));
            const newline = prefix.indexOf(10);
            if (newline >= 0) start += newline + 1;
        }
        if (direction > 0 && start <= old.start && old.end < this.size) {
            start = Math.min(old.end, mode === 'Hex' ? Math.ceil(tail / hexColumns) * hexColumns : tail);
            if (start <= old.start) start = old.end;
        }
        if (direction < 0 && start >= old.start) {
            start = Math.max(0, old.start - capacity / 2);
            if (mode === 'Hex') start -= start % hexColumns;
        }
        this.#loadPage(start, mode, hexColumns, capacity);
        return this.page.indexAt(offset);
    }

    /** @param {number} offset @param {import('./page.js').Mode} mode */
    #loadPage(offset, mode, hexColumns = this.#hexColumns, capacity = mode === 'Hex' ? this.#hexCapacity : pageBytes.Text) {
        this.#page = new Page(this.#document, mode, Math.max(0, Math.min(this.size, offset)), this.#revision + 1, hexColumns, capacity);
        this.#revision++;
    }

    /** @param {string} path @param {boolean} saveAs */
    needsOverwriteConfirmation(path, saveAs) {
        this.#requireIdle();
        if (!path.startsWith('files/')) throw new Error('Save to files/; assets/ is read-only.');
        this.page.commit(this.#document);
        return (saveAs || path !== this.path) && sys.files.exists(path);
    }

    /** @param {string} path @returns {string | null} The published path if the save completed synchronously. */
    startSave(path) {
        this.#requireIdle();
        if (!path.startsWith('files/')) throw new Error('Save to files/; assets/ is read-only.');
        this.page.commit(this.#document);
        const files = sys.files;
        if (!files.openAtomicWrite || !files.writeBlock || !files.finishAtomicWrite) {
            if (this.size > fallbackLimit || files.exists(path))
                throw new Error('This Budo build only supports saving a new file up to 8 MiB.');
            if (!files.writeBinary(path, this.#document.read(0, this.size))) throw fileError('Could not write file');
            return this.#adoptSavedFile(path);
        }
        const handle = files.openAtomicWrite(path);
        if (handle === null) throw fileError('Could not stage save');
        this.#save = { path, handle, position: 0 };
        return null;
    }

    /** @returns {string | null} The published path, or null while writing blocks. */
    advanceSave() {
        const job = this.#save;
        if (!job) throw new Error('There is no active save.');
        if (job.position < this.size) {
            try {
                const bytes = this.#document.read(job.position, blockSize);
                if (!sys.files.writeBlock(job.handle, bytes)) throw fileError('Could not write staged data');
                job.position += bytes.length;
            } catch (error) {
                try {
                    this.cancelSave();
                } catch (cleanup) {
                    throw new Error(error.message + '; ' + cleanup.message);
                }
                throw error;
            }
            return null;
        }
        this.#save = null;
        if (!sys.files.finishAtomicWrite(job.handle, true)) throw fileError('Could not publish save');
        return this.#adoptSavedFile(job.path);
    }

    cancelSave() {
        const job = this.#save;
        if (!job) throw new Error('There is no active save.');
        this.#save = null;
        if (!sys.files.finishAtomicWrite(job.handle, false)) throw fileError('Could not cancel staged save');
    }

    /** @param {string} path */
    #adoptSavedFile(path) {
        let next;
        try {
            next = new Document(path);
            if (next.size !== this.size) {
                next.close();
                throw new Error('The saved file size changed.');
            }
        } catch (error) {
            throw new Error('Saved ' + path + ' but could not reopen it: ' + error.message);
        }
        // Keep the draft and revision so saving does not discard page-local undo.
        this.#setDocument(next);
        return path;
    }
}
