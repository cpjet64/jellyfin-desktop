(function() {
    const idPattern = /^[A-Za-z0-9_-]{1,128}$/;

    function exactKeys(value, keys) {
        return value && typeof value === 'object' && !Array.isArray(value) &&
            Object.keys(value).length === keys.length && keys.every(key => Object.prototype.hasOwnProperty.call(value, key));
    }

    // Explicit-context validation only; this does not authenticate the caller
    // or provide an extraction API for the unmodified Web producer.
    function evaluateCandidate(candidate, account) {
        const keys = ['attemptId', 'serverId', 'accountId', 'itemId', 'mediaSourceId'];
        if (!exactKeys(candidate, keys) || !exactKeys(account, ['serverId', 'accountId'])) return null;
        if (!keys.slice(0, 4).every(key => typeof candidate[key] === 'string' && idPattern.test(candidate[key]))) return null;
        if (candidate.mediaSourceId !== null &&
            (typeof candidate.mediaSourceId !== 'string' || !idPattern.test(candidate.mediaSourceId))) return null;
        if (candidate.serverId !== account.serverId || candidate.accountId !== account.accountId) return null;
        return Object.freeze({ attemptId: candidate.attemptId, serverId: candidate.serverId,
            accountId: candidate.accountId, itemId: candidate.itemId, mediaSourceId: candidate.mediaSourceId,
            sourcePosition: null, eligible: false, reason: 'producer_capability_unavailable' });
    }

    function validReceipt(receipt) {
        return exactKeys(receipt, ['version', 'commandId', 'status']) && receipt.version === 1 &&
            receipt.commandId === '1' && receipt.status === 'accepted';
    }

    function isDependency(value) {
        return value !== null && (typeof value === 'object' || typeof value === 'function');
    }

    class recoveryPlugin {
        constructor({ playbackManager, ServerConnections, events, recoveryBridge } = {}) {
            this.name = 'Recovery Plugin';
            this.type = 'recovery';
            this.id = 'recoveryPlugin';
            this.dependenciesAvailable = [playbackManager, ServerConnections, events].every(isDependency);
            this.bridgeAttached = false;
            this.destroyed = false;
            this.revoked = false;
            this.attachmentStarted = false;
            this.endpoint = null;
            this.handshakeSent = false;
            this.onRevoked = () => { this.revoked = true; this.bridgeAttached = false; this.finishAttachment(); };
            this.onEndpointReady = value => this.startHandshake(value === true);
            this.ready = new Promise(resolve => { this.resolveReady = resolve; });
            this.attach(recoveryBridge);
        }

        finishAttachment() {
            if (!this.resolveReady) return;
            const resolve = this.resolveReady;
            this.resolveReady = null;
            resolve();
        }

        async attach(injectedEndpoint) {
            if (this.attachmentStarted) return;
            this.attachmentStarted = true;
            let endpoint = injectedEndpoint;
            try {
                if (!endpoint) endpoint = (await window.apiPromise)?.recoveryBridge;
                if (this.destroyed || !endpoint || typeof endpoint.invoke !== 'function' ||
                    !endpoint.revoked || typeof endpoint.revoked.connect !== 'function' ||
                    typeof endpoint.revoked.disconnect !== 'function' ||
                    !endpoint.readyChanged || typeof endpoint.readyChanged.connect !== 'function' ||
                    typeof endpoint.readyChanged.disconnect !== 'function' ||
                    typeof endpoint.sessionId !== 'string' || !endpoint.sessionId || endpoint.sessionId.length > 128) {
                    this.finishAttachment();
                    return;
                }
                this.endpoint = endpoint;
                endpoint.revoked.connect(this.onRevoked);
                endpoint.readyChanged.connect(this.onEndpointReady);
                if (this.destroyed || this.revoked) { this.finishAttachment(); return; }
                this.startHandshake();
            } catch (_) {
                // Ordinary playback remains available when this optional boundary is absent.
                this.finishAttachment();
            }
        }

        startHandshake(announced = false) {
            if (!this.resolveReady || this.destroyed || this.revoked || this.handshakeSent || (!announced && this.endpoint?.ready !== true)) return;
            this.handshakeSent = true;
            try {
                this.endpoint.invoke({ version: 1, sessionId: this.endpoint.sessionId, commandId: '1', operation: 'handshake' }, receipt => {
                    if (!this.resolveReady) return;
                    if (!this.destroyed && !this.revoked && validReceipt(receipt)) this.bridgeAttached = true;
                    this.finishAttachment();
                });
            } catch (_) { this.finishAttachment(); }
        }

        status() {
            return Object.freeze({ bridgeAttached: this.bridgeAttached, dependenciesAvailable: this.dependenciesAvailable,
                capability: 'unsupported', candidate: null, eligible: false });
        }

        destroy() {
            this.destroyed = true;
            this.bridgeAttached = false;
            this.finishAttachment();
            try {
                if (this.endpoint?.revoked && typeof this.endpoint.revoked.disconnect === 'function')
                    this.endpoint.revoked.disconnect(this.onRevoked);
                if (this.endpoint?.readyChanged && typeof this.endpoint.readyChanged.disconnect === 'function')
                    this.endpoint.readyChanged.disconnect(this.onEndpointReady);
            } catch (_) {
                // The channel may already have been torn down.
            }
            this.endpoint = null;
        }
    }

    recoveryPlugin.evaluateCandidate = evaluateCandidate;
    window._recoveryPlugin = recoveryPlugin;
})();
