/* Proposed Jellyfin Web recovery helper; see accompanying upstream patch. */
const idPattern = /^[A-Za-z0-9._:-]{1,128}$/;

export const RECOVERY_DESCRIPTOR = Object.freeze({
    capability: 'jellyfin-desktop-recovery', version: 1,
    requestSchema: 'logical-video-v1', resultSchema: 'attempt-outcome-v1'
});

function identity(request) {
    const safe = value => typeof value === 'string' && idPattern.test(value) ? value : null;
    return { hostSession: safe(request?.hostSession), producerGeneration: safe(request?.producerGeneration),
        attemptId: safe(request?.attemptId) };
}

function matches(record, evidence) {
    return evidence && Object.keys(record.key).every(key => evidence[key] === record.key[key]);
}

function valid(request) {
    if (!request || typeof request !== 'object' || Array.isArray(request)) return false;
    const fields = ['hostSession', 'producerGeneration', 'attemptId', 'serverId', 'accountId', 'itemId', 'sourceId'];
    const extra = ['positionTicks', 'state', 'mediaType', 'seekable', 'localPlayer', 'singleItem', 'live', 'remote',
        'tracksVerified', 'timelineVerified', 'audioStreamIndex', 'subtitleStreamIndex'];
    if (Object.keys(request).length !== fields.length + extra.length ||
        Object.keys(request).some(key => !fields.includes(key) && !extra.includes(key))) return false;
    if (fields.some(key => typeof request[key] !== 'string' || !idPattern.test(request[key]))) return false;
    return Number.isSafeInteger(request.positionTicks) && request.positionTicks >= 0 &&
        Number.isSafeInteger(request.audioStreamIndex) && request.audioStreamIndex >= 0 &&
        Number.isSafeInteger(request.subtitleStreamIndex) && request.subtitleStreamIndex >= -1 &&
        ['playing', 'paused'].includes(request.state) && request.mediaType === 'Video' &&
        request.seekable === true && request.localPlayer === true && request.singleItem === true &&
        request.live === false && request.remote === false && request.tracksVerified === true &&
        request.timelineVerified === true;
}

export class RecoveryTransaction {
    constructor(testBoundary = null) {
        this.testBoundary = testBoundary;
        this.current = null;
        this.unsettled = null;
        this.seen = new Set();
    }

    begin(request, start) {
        try { request = { ...request }; } catch (_) { request = null; }
        let trusted = false;
        try {
            trusted = this.testBoundary?.kind === 'w763-test-only' &&
                this.testBoundary.isTrusted(identity(request)) === true;
        } catch (_) { /* unavailable test binding fails closed */ }
        if (!trusted || !valid(request) || this.unsettled || this.seen.size >= 1024 || this.seen.has(request.attemptId)) {
            return Promise.resolve({ ...identity(request), status: 'rejected' });
        }
        this.invalidate();
        this.seen.add(request.attemptId);
        const record = { key: identity(request), active: true, accepted: false, terminal: false,
            settled: false, completed: false, resolve: null };
        const result = new Promise(resolve => { record.resolve = resolve; });
        this.current = record;
        Promise.resolve().then(() => start(record, request)).catch(() => this.reject(record));
        return result;
    }

    owns(record) {
        return this.current === record && record.active;
    }

    allowsEvent(envelope) {
        if (!envelope) return true;
        return !!this.current && this.current.accepted && this.owns(this.current) && matches(this.current, envelope);
    }

    dispatchError(envelope) {
        const record = this.current;
        if (!record || record.accepted || this.unsettled !== record || !this.owns(record) || !matches(record, envelope)) return false;
        record.dispatchError = true;
        return true;
    }

    complete(record, status) {
        if (record.completed) return false;
        record.completed = true;
        record.resolve({ ...record.key, status, ...(record.dispatchError ? { dispatchError: true } : {}) });
        this.releaseSettled(record);
        return true;
    }

    releaseSettled(record) {
        if (!record.completed || !record.terminal || !record.settled || this.unsettled !== record) return;
        if (this.current === record) this.current = null;
        record.active = false;
        this.unsettled = null;
    }

    accept(record) {
        if (!this.owns(record) || this.unsettled !== record) return false;
        record.accepted = true;
        return this.complete(record, 'accepted');
    }

    dispatching(record) {
        if (!this.owns(record) || this.unsettled) return false;
        this.unsettled = record;
        return true;
    }

    reject(record) {
        if (!this.owns(record)) return false;
        record.active = false;
        this.current = null;
        return this.complete(record, 'rejected');
    }

    invalidate() {
        const record = this.current;
        if (!record) return;
        record.active = false;
        this.current = null;
        if (record.transitionCleanup) {
            const cleanup = record.transitionCleanup;
            record.transitionCleanup = null;
            try { cleanup(); } catch (_) { /* invalidation still settles */ }
        }
        if (!record.accepted) this.complete(record, 'canceled');
    }

    revoke(context) {
        const record = this.current || this.unsettled;
        if (!record || context?.hostSession !== record.key.hostSession ||
            context?.producerGeneration !== record.key.producerGeneration) return false;
        if (this.current === record) this.invalidate();
        return true;
    }

    observe(evidence) {
        const record = this.unsettled;
        if (!record || !matches(record, evidence)) return false;
        try {
            if (this.testBoundary?.verifyEvidence(evidence) !== true) return false;
        } catch (_) { return false; }
        if (evidence.kind === 'terminal' && !record.terminal) record.terminal = true;
        else if (evidence.kind === 'settled' && !record.settled) record.settled = true;
        else return false;
        this.releaseSettled(record);
        return true;
    }
}
