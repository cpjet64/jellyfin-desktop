(function() {
    class PlaybackErrorPrompt {
        constructor(player, confirm, intent) {
            this.player = player;
            this.confirm = confirm;
            this.intent = intent;
        }

        invalidate() { this.intent.invalidatePrompt(); }

        async handle(error) {
            const prompt = this.intent.beginPrompt();
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
            if (this.intent.isCurrentPrompt(prompt)) {
                this.player.events.trigger(this.player, 'error', [errorData]);
            }
        }
    }
    window._PlaybackErrorPrompt = PlaybackErrorPrompt;
})();
