import { decodeText, encodeText, formatHex, parseHex, textBoundary } from './document.js';

/** @typedef {'Text' | 'Hex'} Mode */
/** @typedef {import('./document.js').Document} Document */

export const pageBytes = { Text: 8192, Hex: 512 };
export const draftLimit = 16384;

/** @param {string} char */
function utf8Width(char) {
    const code = char.codePointAt(0);
    return code <= 0x7F ? 1 : code <= 0x7FF ? 2 : code <= 0xFFFF ? 3 : 4;
}

/** An editable string backed by a bounded snapshot of the document's bytes. */
export class Page {
    #bytes;
    #baseline;

    /** @param {Document} document @param {Mode} mode @param {number} offset @param {number} revision */
    constructor(document, mode, offset, revision, hexColumns = 16, capacity = pageBytes[mode]) {
        this.mode = mode;
        this.revision = revision;
        const align = mode === 'Text' ? offset => textBoundary(document, offset) : offset => offset;
        this.start = align(offset);
        const end = align(Math.min(document.size, this.start + capacity));
        this.#bytes = document.read(this.start, end - this.start);
        this.problem = '';
        let value = '';
        if (mode === 'Hex') value = formatHex(this.#bytes, hexColumns);
        else {
            try {
                value = decodeText(this.#bytes);
            } catch (error) {
                this.problem = error.message;
            }
        }
        this.#baseline = value;
        this.model = { value };
    }

    get end() { return this.start + this.#bytes.length; }
    get pending() { return this.model.value !== this.#baseline; }

    /** @param {number} index UTF-16 position in the current draft. */
    byteAt(index) {
        if (this.mode === 'Hex') return this.caretAt(index).byte;
        if (this.problem) return this.start;
        const prefix = this.model.value.slice(0, index);
        // A live draft may contain invalid UTF-16; commit validates it before moving the window.
        let length = 0;
        for (const char of prefix) length += utf8Width(char);
        return this.start + length;
    }

    /** @param {number} index UTF-16 position in the editable draft. */
    caretAt(index) {
        if (this.mode === 'Text') {
            // A platform text session can report an index inside a surrogate pair.
            if (/[\uDC00-\uDFFF]/.test(this.model.value[index] || '') &&
                /[\uD800-\uDBFF]/.test(this.model.value[index - 1] || '')) index--;
            return { byte: this.byteAt(index), nibble: 0 };
        }
        const digits = this.model.value.slice(0, index).replace(/[^0-9a-f]/gi, '').length;
        return { byte: this.start + Math.floor(digits / 2), nibble: digits % 2 };
    }

    /** @param {number} offset Byte position in a freshly loaded, formatted window. */
    indexAt(offset) {
        if (this.mode === 'Hex') return Math.min(this.model.value.length, Math.max(0, (offset - this.start) * 3));
        let byte = this.start, index = 0;
        for (const char of this.model.value) {
            const length = utf8Width(char);
            if (byte + length > offset) break;
            byte += length;
            index += char.length;
        }
        return index;
    }

    /** @param {Document} document */
    commit(document) {
        if (!this.pending) return;
        const text = this.model.value;
        const bytes = this.mode === 'Text' ? encodeText(text) : parseHex(text);
        document.replace(this.start, this.#bytes, bytes);
        this.#bytes = bytes;
        this.#baseline = text;
    }
}
