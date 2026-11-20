/** @typedef {{ offset: number, length: number, data?: Uint8Array }} Piece */

export class Document {
    constructor(path = '') {
        this.path = path;
        this.size = 0;
        this.handle = null;
        this.dirty = false;
        /** @type {Piece[]} */
        this.pieces = [];
        /** @type {number[]} */
        this.ends = [];
        if (!path) return;

        const handle = sys.files.openRead(path);
        if (handle === null) throw new Error(sys.files.getError() || 'Cannot open file');
        const { size } = sys.files.getReadInfo(handle);
        if (!Number.isSafeInteger(size) || size < 0) {
            sys.files.closeRead(handle);
            throw new Error('File is too large for exact byte offsets');
        }
        this.handle = handle;
        this.size = size;
        if (size) {
            this.pieces.push({ offset: 0, length: size });
            this.ends.push(size);
        }
    }

    close() {
        if (this.handle !== null) sys.files.closeRead(this.handle);
        this.handle = null;
    }

    indexAt(position) {
        let low = 0, high = this.ends.length;
        while (low < high) {
            const mid = Math.floor((low + high) / 2);
            if (this.ends[mid] <= position) low = mid + 1;
            else high = mid;
        }
        return low;
    }

    read(position, length) {
        const bytes = new Uint8Array(Math.min(length, this.size - position));
        let index = this.indexAt(position), filled = 0;
        while (filled < bytes.length) {
            const piece = this.pieces[index];
            const within = position + filled - (this.ends[index] - piece.length);
            const count = Math.min(piece.length - within, bytes.length - filled);
            const offset = piece.offset + within;
            if (piece.data) bytes.set(piece.data.subarray(offset, offset + count), filled);
            else {
                if (!sys.files.seek(this.handle, offset)) throw new Error('Cannot seek source file');
                const block = sys.files.read(this.handle, count);
                if (!block || block.byteLength !== count) throw new Error('Could not read source bytes; the file may have changed');
                bytes.set(new Uint8Array(block), filled);
            }
            filled += count;
            index++;
        }
        return bytes;
    }

    /** @param {Uint8Array} before @param {Uint8Array} after */
    replace(position, before, after) {
        let first = 0, last = 0;
        while (first < before.length && first < after.length && before[first] === after[first]) first++;
        while (last < before.length - first && last < after.length - first &&
            before[before.length - last - 1] === after[after.length - last - 1]) last++;
        const from = position + first, to = position + before.length - last;
        const added = after.slice(first, after.length - last);
        if (from === to && !added.length) return;

        /** @type {Piece[]} */
        const pieces = [];
        /** @param {Piece} piece */
        const append = piece => {
            if (!piece.length) return;
            const prev = pieces[pieces.length - 1];
            if (prev && prev.data === piece.data && prev.offset + prev.length === piece.offset) prev.length += piece.length;
            else pieces.push({ ...piece });
        };
        let pos = 0, inserted = false;
        const insert = () => {
            if (!inserted) append({ offset: 0, length: added.length, data: added });
            inserted = true;
        };
        for (const piece of this.pieces) {
            const end = pos + piece.length;
            if (pos < from) append({ ...piece, length: Math.min(end, from) - pos });
            if (end > from) insert();
            if (end > to) {
                const skip = Math.max(0, to - pos);
                append({ ...piece, offset: piece.offset + skip, length: piece.length - skip });
            }
            pos = end;
        }
        insert();
        const buffers = new Set(pieces.map(piece => piece.data));
        let memory = 0;
        for (const bytes of buffers) if (bytes) memory += bytes.byteLength;
        if (pieces.length > 50000 || memory > 64 * 1024 * 1024)
            throw new Error('Too many unsaved edits; save and reopen the document');
        let size = 0;
        const ends = pieces.map(piece => (size += piece.length));
        if (!Number.isSafeInteger(size)) throw new Error('File is too large for exact byte offsets');
        this.pieces = pieces;
        this.ends = ends;
        this.size = size;
        this.dirty = true;
    }
}

const HEX = Array.from({ length: 256 }, (_, i) => i.toString(16).padStart(2, '0').toUpperCase());
/** @param {Uint8Array} bytes @param {number} columns */
export function formatHex(bytes, columns = 16) {
    return Array.from(bytes, (byte, i) => (i ? (i % columns ? ' ' : '\n') : '') + HEX[byte]).join('');
}

/** @param {string} text */
export function hexCells(text) {
    /** @type {{start: number, end: number, char: string}[]} */
    const cells = [];
    let first = -1;
    let invalid = false;
    for (const match of text.matchAll(/[0-9a-f]|[^\s]/gi)) {
        if (!/^[0-9a-f]$/i.test(match[0])) {
            invalid = true;
            continue;
        }
        const index = match.index;
        if (first < 0) first = index;
        else {
            const byte = parseInt(text[first] + text[index], 16);
            cells.push({ start: first, end: index + 1, char: invalid ? '?' : byte >= 32 && byte <= 126 ? String.fromCharCode(byte) : '.' });
            first = -1;
        }
    }
    if (first >= 0) cells.push({ start: first, end: first + 1, char: '?' });
    return cells;
}

/** @param {number} address @param {number} width */
export function formatByteAddress(address, width) {
    return address.toString(10) + '  0x' + address.toString(16).toUpperCase().padStart(width, '0');
}

/** @param {string} text @param {number[]} starts @param {number} offset @param {number} width */
export function hexRowAddresses(text, starts, offset, width) {
    let index = 0, digits = 0;
    return starts.map(start => {
        while (index < start) {
            const code = text.charCodeAt(index++);
            if ((code >= 48 && code <= 57) || ((code | 32) >= 97 && (code | 32) <= 102)) digits++;
        }
        // A soft wrap can start on the second nibble of an incomplete byte pair.
        const address = offset + Math.floor(digits / 2);
        return formatByteAddress(address, width);
    });
}

export function parseHex(text) {
    const digits = text.replace(/\s/g, '');
    if (digits.length % 2 || /[^0-9a-f]/i.test(digits))
        throw new Error('Hexadecimal needs complete byte pairs (00–FF). Whitespace is ignored.');
    const bytes = new Uint8Array(digits.length / 2);
    for (let i = 0; i < bytes.length; i++) bytes[i] = parseInt(digits.slice(i * 2, i * 2 + 2), 16);
    return bytes;
}

export function decodeText(bytes) {
    if (bytes.includes(0)) throw new Error('This page contains NUL bytes; use Hex mode');
    try {
        return decodeURIComponent(Array.from(bytes, byte => '%' + HEX[byte]).join(''));
    } catch (error) {
        if (!(error instanceof URIError)) throw error;
        throw new Error('This page is not valid UTF-8; use Hex mode');
    }
}

export function encodeText(text) {
    if (text.includes('\0')) throw new Error('Use Hex mode to insert NUL bytes');
    let encoded;
    try {
        encoded = encodeURIComponent(text);
    } catch (error) {
        if (!(error instanceof URIError)) throw error;
        throw new Error('Text contains an incomplete Unicode character');
    }
    const bytes = new Uint8Array(encoded.length);
    let size = 0;
    for (let i = 0; i < encoded.length; i++) {
        if (encoded[i] === '%') {
            bytes[size++] = parseInt(encoded.slice(i + 1, i + 3), 16);
            i += 2;
        } else bytes[size++] = encoded.charCodeAt(i);
    }
    return bytes.subarray(0, size);
}

/** Shift a text boundary back, but never scan an arbitrary run of invalid bytes. */
export function textBoundary(doc, position) {
    if (position <= 0 || position >= doc.size) return position;
    const start = Math.max(0, position - 3);
    const bytes = doc.read(start, position - start + 1);
    let index = bytes.length - 1;
    if (bytes[index] === 10 && bytes[index - 1] === 13) return position - 1;
    while (index > 0 && (bytes[index] & 0xC0) === 0x80) index--;
    const lead = bytes[index], distance = bytes.length - 1 - index;
    const width = lead >= 0xF0 && lead <= 0xF4 ? 4 : lead >= 0xE0 && lead <= 0xEF ? 3
        : lead >= 0xC2 && lead <= 0xDF ? 2 : 0;
    return distance < width ? start + index : position;
}
