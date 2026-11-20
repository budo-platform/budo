const decoderReady = BudoImageDecoder();
let encoded = 0;
let encodedLength = 0;
let dimensions = null;
let currentRequest = 0;

self.onmessage = async ({ data }) => {
    let decoder;
    let source = 0;
    let output = 0;
    try {
        decoder = await decoderReady;
        if (data.type === 'probe') {
            currentRequest = data.request;
            encodedLength = data.bytes.byteLength;
            encoded = decoder._malloc(encodedLength);
            const info = decoder._malloc(8);
            if (!encoded || !info) {
                decoder._free(info);
                throw new Error('image.out_of_memory');
            }
            decoder.HEAPU8.set(new Uint8Array(data.bytes), encoded);
            data.bytes = null;
            const valid = decoder._image_probe(encoded, encodedLength, info, info + 4);
            const sourceWidth = decoder.HEAP32[info >> 2];
            const sourceHeight = decoder.HEAP32[(info + 4) >> 2];
            decoder._free(info);
            if (!valid) throw new Error('image.invalid_data');
            if (sourceWidth < 1 || sourceHeight < 1 || sourceWidth > 32768 || sourceHeight > 32768 ||
                sourceWidth * sourceHeight > data.maxPixels) throw new Error('image.pixel_limit');
            const scale = Math.min(1, data.maxWidth ? data.maxWidth / sourceWidth : 1,
                data.maxHeight ? data.maxHeight / sourceHeight : 1);
            dimensions = {
                sourceWidth, sourceHeight, width: Math.max(1, Math.floor(sourceWidth * scale)),
                height: Math.max(1, Math.floor(sourceHeight * scale))
            };
            self.postMessage({ type: 'metadata', request: currentRequest, ...dimensions });
            return;
        }
        if (data.type !== 'decode' || data.request !== currentRequest || !encoded || !dimensions)
            throw new Error('image.decode_failed');
        const info = decoder._malloc(8);
        if (!info) throw new Error('image.out_of_memory');
        const { width, height } = dimensions;
        source = decoder._image_load_rgba8_scaled_from_memory(encoded, encodedLength, width, height, info, info + 4);
        const sourceWidth = decoder.HEAP32[info >> 2];
        const sourceHeight = decoder.HEAP32[(info + 4) >> 2];
        decoder._free(info);
        if (!source || sourceWidth < width || sourceHeight < height ||
            sourceWidth > dimensions.sourceWidth || sourceHeight > dimensions.sourceHeight)
            throw new Error('image.decode_failed');
        const length = width * height * 4;
        output = source;
        if (width !== sourceWidth || height !== sourceHeight) {
            output = decoder._malloc(length);
            if (!output) throw new Error('image.out_of_memory');
            for (let row = 0; row < height; row++)
                decoder._image_resize_rgba8_row(source, sourceWidth, sourceHeight, output, width, height, row);
            decoder._image_free(source);
            source = 0;
        }
        const pixels = decoder.HEAPU8.slice(output, output + length).buffer;
        self.postMessage({ type: 'complete', request: currentRequest, pixels }, [pixels]);
    } catch (error) {
        const code = error.message && error.message.startsWith('image.') ? error.message : 'image.decode_failed';
        self.postMessage({ type: 'error', request: data.request, code });
    } finally {
        if (decoder && (data.type !== 'probe' || !dimensions)) {
            if (output && output !== source) decoder._free(output);
            decoder._image_free(source);
            decoder._free(encoded);
            encoded = 0;
            dimensions = null;
        }
    }
};