export function createPlayer(video, background, inset, output) {
    const state = {
        preview: null, export: null, texture: 0, durationUs: 0, positionUs: 0,
        paused: false, ended: false, seeking: false,
        previewStatus: 'Preview not started.', exportStatus: 'No export running.',
    };

    const describe = error => error && error.message
        ? (error.code ? `${error.code}: ${error.message}` : error.message) : String(error);
    const failure = fallback => video.getError() || fallback;

    function close(status = 'Preview closed.') {
        const session = state.preview;
        if (!session) return true;
        if (!session.id) {
            session.closeStatus = status;
            return true;
        }
        try {
            if (!video.closePreview(session.id)) {
                state.previewStatus = `${status} Close failed: ${failure('Unknown error')}`;
                return false;
            }
        } catch (error) {
            state.previewStatus = `${status} Close failed: ${describe(error)}`;
            return false;
        }
        if (state.preview === session) {
            state.preview = null;
            state.texture = 0;
            state.durationUs = 0;
            state.positionUs = 0;
            state.paused = false;
            state.ended = false;
            state.seeking = false;
            state.previewStatus = status;
        }
        return true;
    }

    function start() {
        if (state.preview) {
            state.previewStatus = 'A preview is already active.';
            return false;
        }
        const session = { id: 0, closeStatus: null };
        state.preview = session;
        state.previewStatus = 'Opening preview...';
        const callbacks = {
            onReady(durationUs) {
                if (state.preview !== session || session.closeStatus) return;
                state.durationUs = Number.isFinite(durationUs) && durationUs > 0
                    ? Math.floor(durationUs) : 0;
                state.previewStatus = state.durationUs ? 'Preview ready.' : 'Preview duration unavailable.';
            },
            onFrame(texture, ptsUs) {
                if (state.preview !== session || session.closeStatus) return;
                state.texture = texture;
                if (Number.isFinite(ptsUs))
                    state.positionUs = Math.max(0, Math.min(lastPosition(), Math.floor(ptsUs)));
                state.seeking = false;
                state.ended = false;
                state.previewStatus = state.paused ? 'Preview paused.' : 'Preview playing.';
            },
            onEnd() {
                if (state.preview !== session || session.closeStatus) return;
                state.ended = true;
                state.seeking = false;
                state.previewStatus = 'Preview ended. Seek or play again.';
            },
            onError(error) {
                if (state.preview === session) close(`Preview error: ${describe(error)}`);
            },
        };
        try {
            const id = video.preview(background, inset, callbacks);
            session.id = id;
            if (!id) {
                if (state.preview === session) {
                    state.preview = null;
                    state.previewStatus = failure('Could not start preview');
                }
                return false;
            }
            if (session.closeStatus) return close(session.closeStatus);
            return true;
        } catch (error) {
            if (state.preview === session) {
                state.preview = null;
                state.previewStatus = `Could not start preview: ${describe(error)}`;
            }
            return false;
        }
    }

    function lastPosition() {
        return Math.max(0, state.durationUs - 1);
    }

    function seek(positionUs) {
        const session = state.preview;
        if (!session || !session.id || !state.durationUs || !Number.isFinite(positionUs)) return false;
        const target = Math.max(0, Math.min(lastPosition(), Math.round(positionUs)));
        try {
            if (!video.seekPreview(session.id, target)) {
                state.previewStatus = failure('Could not seek preview');
                return false;
            }
        } catch (error) {
            state.previewStatus = `Could not seek preview: ${describe(error)}`;
            return false;
        }
        if (state.preview === session) {
            state.texture = 0;
            state.positionUs = target;
            state.seeking = true;
            state.ended = false;
            state.previewStatus = 'Seeking from previous keyframe...';
        }
        return true;
    }

    function toggle() {
        const session = state.preview;
        if (!session || !session.id || !state.durationUs) return;
        const wasEnded = state.ended;
        if (wasEnded && !seek(0)) return;
        // Seeking an ended, playing preview restarts it without changing its state.
        if (wasEnded && !state.paused) return;
        try {
            const ok = state.paused ? video.resumePreview(session.id) : video.pausePreview(session.id);
            if (!ok) {
                state.previewStatus = failure('Could not change preview playback state');
                return;
            }
            if (state.preview === session) {
                state.paused = !state.paused;
                state.previewStatus = state.paused ? 'Preview paused.' : 'Preview playing.';
            }
        } catch (error) {
            state.previewStatus = `Could not change playback: ${describe(error)}`;
        }
    }

    function restart() {
        if (!state.preview) return start();
        return seek(0);
    }

    function startExport() {
        if (state.export) return;
        const job = { id: 0, cancelling: false };
        state.export = job;
        state.exportStatus = 'Starting export...';
        const callbacks = {
            onProgress(frames) {
                if (state.export === job && !job.cancelling) state.exportStatus = `Exporting frame ${frames}...`;
            },
            onComplete(path) {
                if (state.export !== job) return;
                state.export = null;
                state.exportStatus = `Export complete: ${path}`;
                console.log(state.exportStatus);
            },
            onError(error) {
                if (state.export !== job) return;
                state.export = null;
                state.exportStatus = `Export error: ${describe(error)}`;
                console.log(state.exportStatus);
            },
        };
        try {
            job.id = video.exportMp4(background, inset, output, callbacks);
            if (!job.id && state.export === job) {
                state.export = null;
                state.exportStatus = failure('Could not start export');
            } else if (state.export === job) state.exportStatus = 'Export started.';
        } catch (error) {
            if (state.export === job) {
                state.export = null;
                state.exportStatus = `Could not start export: ${describe(error)}`;
            }
        }
    }

    function cancelExport() {
        const job = state.export;
        if (!job || !job.id || job.cancelling) return;
        try {
            if (!video.cancelExport(job.id)) {
                state.exportStatus = failure('Export cancellation was not accepted');
                return;
            }
            if (state.export === job) {
                job.cancelling = true;
                state.exportStatus = 'Cancellation requested...';
            }
        } catch (error) {
            state.exportStatus = `Could not cancel export: ${describe(error)}`;
        }
    }

    return { state, start, close, seek, toggle, restart, startExport, cancelExport, lastPosition };
}
