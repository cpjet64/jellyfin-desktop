(function() {
    class PlaybackErrorPrompt {
        constructor(player, confirm) {
            this.player = player;
            this.confirm = confirm;
            this.intentRevision = 0;
            this.promptRevision = 0;
        }

        invalidate() { ++this.intentRevision; }

        async handle(error) {
            const intent = this.intentRevision;
            const prompt = ++this.promptRevision;
            this.player.removeMediaDialog();
            console.error(`media error: ${error}`);
            const errorData = { type: 'mediadecodeerror' };
            const confirm = this.confirm;
            try {
                await confirm({
                    title: "Playback Failed",
                    text: `Playback failed with error "${error}". Retry with transcode? (Note this may hang the player.)`,
                    cancelText: "Cancel",
                    confirmText: "Retry"
                });
            } catch (ex) {
                errorData.streamInfo = { mediaSource: { SupportsTranscoding: false } };
            }
            if (intent === this.intentRevision && prompt === this.promptRevision) {
                this.player.events.trigger(this.player, 'error', [errorData]);
            }
        }
    }
    window._PlaybackErrorPrompt = PlaybackErrorPrompt;
})();
