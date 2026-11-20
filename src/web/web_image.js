Module['budoImageBackend'] = (() => {
    const services = new Map();
    let nextService = 1;
    const available = () => typeof Worker === 'function' && typeof WebAssembly === 'object' &&
        typeof Blob === 'function' && typeof URL.createObjectURL === 'function';
    const stop = slot => {
        if (slot.worker) slot.worker.terminate();
        slot.worker = null;
        slot.request = 0;
        slot.event = null;
    };
    const start = (service, slot) => {
        const worker = new Worker(service.url);
        slot.worker = worker;
        worker.onmessage = ({data}) => {
            if (services.get(service.id) !== service || slot.worker !== worker || data.request !== slot.request)
                return;
            slot.event = data;
        };
        const fail = event => {
            event.preventDefault();
            if (services.get(service.id) === service && slot.worker === worker) {
                worker.terminate();
                slot.worker = null;
                slot.event = {type: 'error', code: 'image.decode_failed'};
            }
        };
        worker.onerror = fail;
        worker.onmessageerror = fail;
    };
    return {
        available,
        create() {
            if (!available() || nextService > 0x7fffffff) return 0;
            const service = {id: nextService++, slots: [{}, {}], url: null};
            try {
                service.url = URL.createObjectURL(new Blob([FS.readFile('/budo-image-worker.js')],
                    {type: 'text/javascript'}));
                for (const slot of service.slots) start(service, slot);
                services.set(service.id, service);
                return service.id;
            } catch (error) {
                for (const slot of service.slots) stop(slot);
                if (service.url) URL.revokeObjectURL(service.url);
                return 0;
            }
        },
        destroy(handle) {
            const service = services.get(handle);
            if (!service) return;
            services.delete(handle);
            for (const slot of service.slots) stop(slot);
            URL.revokeObjectURL(service.url);
        },
        submit(handle, request, pointer, length, maxWidth, maxHeight, maxPixels) {
            const service = services.get(handle);
            const slot = service.slots.find(candidate => !candidate.request);
            if (!slot) return 0;
            slot.request = request;
            slot.event = null;
            try {
                if (!slot.worker) start(service, slot);
                const bytes = HEAPU8.slice(pointer, pointer + length).buffer;
                slot.worker.postMessage({type: 'probe', request, bytes, maxWidth, maxHeight, maxPixels}, [bytes]);
            } catch (error) {
                slot.event = {type: 'error', code: 'image.out_of_memory'};
            }
            return 1;
        },
        status(handle, request, info) {
            const slot = services.get(handle).slots.find(candidate => candidate.request === request);
            if (!slot || !slot.event) return 0;
            const event = slot.event;
            if (event.type === 'metadata') {
                HEAP32.set([event.width, event.height, event.sourceWidth, event.sourceHeight], info >> 2);
                slot.event = null;
                return 1;
            }
            if (event.type === 'complete') return 2;
            return ({'image.invalid_data': 3, 'image.pixel_limit': 4, 'image.out_of_memory': 5})[event.code] || 6;
        },
        decode(handle, request) {
            const slot = services.get(handle).slots.find(candidate => candidate.request === request);
            try {
                slot.worker.postMessage({type: 'decode', request});
            } catch (error) {
                slot.event = {type: 'error', code: 'image.decode_failed'};
            }
        },
        copy(handle, request, pointer, length) {
            const slot = services.get(handle).slots.find(candidate => candidate.request === request);
            if (!slot || !slot.event || !slot.event.pixels || slot.event.pixels.byteLength !== length) return 0;
            HEAPU8.set(new Uint8Array(slot.event.pixels), pointer);
            slot.event = null;
            slot.request = 0;
            return 1;
        },
        cancel(handle, request) {
            const slot = services.get(handle).slots.find(candidate => candidate.request === request);
            if (slot) stop(slot);
        }
    };
})();