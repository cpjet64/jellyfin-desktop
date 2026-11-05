(function() {
    // Local ordering only. These tickets are not producer identities and are
    // never sent to native code while the bridge lifetime is unqualified.
    class PlaybackIntent {
        constructor() {
            this.nextId = 0;
            this.request = null;
            this.exhausted = false;
            this.lastAction = null;
            this.lastLoadCallback = null;
            this.activePrompt = null;
        }

        ticket(operation, origin, targetRequest) {
            if (this.exhausted || this.nextId >= Number.MAX_SAFE_INTEGER) {
                this.exhausted = true;
                this.request = null;
                return null;
            }
            this.lastAction = Object.freeze({ commandId: ++this.nextId, targetRequest, operation, origin });
            return this.lastAction;
        }

        play() {
            const ticket = this.ticket('play', 'web_play_unqualified', null);
            this.request = ticket ? ticket.commandId : null;
            this.lastLoadCallback = null;
            return ticket;
        }

        capture(operation, origin) {
            return this.ticket(operation, origin, this.request);
        }

        barrier(operation, origin) {
            const ticket = this.capture(operation, origin);
            this.request = null;
            this.lastLoadCallback = null;
            return ticket;
        }

        unattributed(operation, origin) {
            const ticket = this.ticket(operation, origin, null);
            this.request = null;
            this.lastLoadCallback = null;
            return ticket;
        }

        loadCallback(ticket) {
            if (ticket && ticket.commandId === this.request) {
                this.lastLoadCallback = ticket.commandId;
            }
        }

        beginPrompt() {
            this.activePrompt = this.ticket('error', 'web_error_unattributed', null);
            return this.activePrompt;
        }

        invalidatePrompt() { this.activePrompt = null; }

        isCurrentPrompt(ticket) { return ticket !== null && this.activePrompt === ticket; }
    }
    window._PlaybackIntent = PlaybackIntent;
})();
