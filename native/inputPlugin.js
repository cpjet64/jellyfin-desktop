(function() {
const remap = {
    play_pause: 'playpause', seek_forward: 'fastforward', seek_backward: 'rewind',
    'host:fullscreen': 'togglefullscreen', cycle_audio: 'changeaudiotrack',
    cycle_subtitles: 'changesubtitletrack', increase_volume: 'volumeup',
    decrease_volume: 'volumedown', step_backward: 'previouschapter',
    step_forward: 'nextchapter', enter: 'select'
};

class inputPlugin {
    constructor({ inputManager, playbackManager, events }) {
        this.name = 'Input Plugin';
        this.type = 'input';
        this.id = 'inputPlugin';
        this.inputManager = inputManager;
        this.playbackManager = playbackManager;
        this.events = events;
        this.listeners = [];
        this.playerListeners = [];
        this.signals = [];
        this.attachedPlayer = null;
        this.durationCheckInterval = null;
        this.positionUpdateInterval = null;
        this.lastReportedPosition = 0;
        this.lastDuration = 0;
        this.destroyed = false;
        this.initialize();
    }

    async initialize() {
        const api = await window.apiPromise;
        if (this.destroyed) return;
        this.api = api;
        this.attachNativeSignals();
        this.attachSettingsListener();
        this.attachManagerListeners();
        api.system.hello('jmpInputPlugin');
    }

    listen(target, event, callback, player = false) {
        const owned = (...args) => {
            if (!this.destroyed && (!player || this.attachedPlayer === target)) callback(...args);
        };
        this.events.on(target, event, owned);
        (player ? this.playerListeners : this.listeners).push([target, event, owned]);
    }

    unlisten(listeners) {
        for (const [target, event, callback] of listeners) this.events.off(target, event, callback);
        listeners.length = 0;
    }

    connect(signal, callback) {
        const owned = (...args) => { if (!this.destroyed) callback(...args); };
        signal.connect(owned);
        this.signals.push([signal, owned]);
    }

    attachNativeSignals() {
        const { input, player } = this.api;
        this.connect(input.hostInput, actions => actions.forEach(action => this.handleAction(action)));
        this.connect(player.playbackRateChanged, rate => {
            const current = this.playbackManager._currentPlayer;
            if (current && current._playRate !== undefined) current._playRate = rate;
            if (current) this.events.trigger(current, 'ratechange');
        });
        this.connect(input.volumeChanged, volume => {
            const current = this.playbackManager._currentPlayer;
            if (current && typeof current.setVolume === 'function') current.setVolume(volume);
        });
        this.connect(input.rateChanged, rate => {
            const current = this.playbackManager._currentPlayer;
            if (current && typeof current.setPlaybackRate === 'function') current.setPlaybackRate(rate);
        });
        this.connect(input.positionSeek, position => this.onPositionSeek(position));
    }

    handleAction(action) {
        const manager = this.playbackManager;
        if (action === 'shuffle' || action === 'sorted') {
            manager.setQueueShuffleMode(action === 'shuffle' ? 'Shuffle' : 'Sorted');
        } else if (action === 'previous') {
            const current = manager._currentPlayer;
            if (current && manager.isPlayingAudio(current) &&
                (manager.currentTime(current) >= 5000 || manager.getCurrentPlaylistIndex(current) <= 0)) {
                manager.seekPercent(0, current);
            } else if (current) {
                manager.previousTrack(current);
            }
        } else {
            this.inputManager.handleCommand(Object.prototype.hasOwnProperty.call(remap, action) ? remap[action] : action, {});
        }
    }

    attachSettingsListener() {
        let last = window.jmpInfo.settings.main.fullscreen;
        this.settingsCallback = section => {
            if (this.destroyed || section !== 'main') return;
            const current = window.jmpInfo.settings.main.fullscreen;
            if (current !== last) {
                last = current;
                const player = this.playbackManager._currentPlayer;
                if (player) this.events.trigger(player, 'fullscreenchange');
            }
        };
        window.jmpInfo.settingsUpdate.push(this.settingsCallback);
    }

    attachManagerListeners() {
        const manager = this.playbackManager;
        for (const event of ['playlistitemremove', 'playlistitemadd', 'playlistitemchange']) {
            this.listen(manager, event, () => this.updateQueueState());
        }
        this.listen(manager, 'playbackstop', (e, info) => {
            if (info && info.player === this.attachedPlayer) this.resetPlaybackTimers();
            this.updateQueueState();
            this.api.player.notifyPlaybackStop(!!(info && info.nextMediaType));
        });
        this.listen(manager, 'playbackstart', (e, player) => this.onPlaybackStart(player));
    }

    updateQueueState() {
        try {
            const queue = this.playbackManager._playQueueManager;
            const playlist = queue?.getPlaylist();
            const index = queue?.getCurrentPlaylistIndex();
            if (!Array.isArray(playlist) || index == null || index < 0) return;
            const state = this.playbackManager.getPlayerState();
            const music = state?.NowPlayingItem?.MediaType === 'Audio';
            this.api.player.notifyQueueChange(index < playlist.length - 1, music || index > 0);
        } catch (error) {
            console.error('PlayerMedia: Error in updateQueueState:', error);
        }
    }

    onPlaybackStart(player) {
        if (!player) return;
        const manager = this.playbackManager;
        const state = manager.getPlayerState();
        if (state && state.NowPlayingItem) {
            this.api.player.notifyMetadata(state.NowPlayingItem);
            const position = manager.currentTime();
            if (position != null) {
                this.api.player.notifyPosition(Math.floor(position));
                this.lastReportedPosition = position;
            }
            if (typeof player.getVolume === 'function') this.api.player.notifyVolumeChange(player.getVolume() / 100);
        }
        if (player !== this.attachedPlayer) this.attachPlayer(player);
    }

    attachPlayer(player) {
        this.clearTimers();
        this.unlisten(this.playerListeners);
        this.attachedPlayer = player;
        this.lastDuration = 0;
        this.listen(player, 'shufflequeuemodechange', () => {
            this.api.player.notifyShuffleChange(this.playbackManager.getQueueShuffleMode() === 'Shuffle');
        }, true);
        this.listen(player, 'repeatmodechange', () => this.api.player.notifyRepeatChange(this.playbackManager.getRepeatMode()), true);
        this.listen(player, 'playing', () => this.onPlaying(), true);
        this.listen(player, 'pause', () => this.onPause(), true);
        this.listen(player, 'playbackstop', () => {
            this.resetPlaybackTimers();
        }, true);
        this.listen(player, 'volumechange', () => {
            if (typeof player.getVolume === 'function') this.api.player.notifyVolumeChange(player.getVolume() / 100);
        }, true);
        this.listen(player, 'ratechange', () => {
            if (typeof player.getPlaybackRate === 'function') this.api.player.notifyRateChange(player.getPlaybackRate());
        }, true);
        this.listen(player, 'timeupdate', () => this.reportPosition(false), true);
    }

    onPlaying() {
        this.api.player.notifyPlaybackState('Playing');
        this.updateQueueState();
        this.api.player.notifyRateChange(1.0);
        this.clearTimers();
        this.checkDuration();
        this.durationCheckInterval = setInterval(() => this.checkDuration(), 1000);
        const position = this.playbackManager.currentTime();
        if (position != null) this.api.player.notifyPosition(Math.floor(position));
        this.positionUpdateInterval = setInterval(() => this.reportPosition(true), 500);
    }

    onPause() {
        this.api.player.notifyPlaybackState('Paused');
        this.api.player.notifyRateChange(0.0);
        if (this.durationCheckInterval) clearInterval(this.durationCheckInterval);
        this.durationCheckInterval = null;
        const position = this.playbackManager.currentTime();
        if (position != null) this.lastReportedPosition = position;
    }

    checkDuration() {
        const duration = this.playbackManager.duration();
        if (duration && duration !== this.lastDuration) {
            this.lastDuration = duration;
            this.api.player.notifyDurationChange(Math.floor(duration / 10000));
        }
    }

    reportPosition(periodic) {
        const position = this.playbackManager.currentTime();
        if (position == null) return;
        const difference = Math.abs(position - this.lastReportedPosition);
        if (this.lastReportedPosition > 0 && difference > 2000) {
            this.api.player.notifyRateChange(0.0);
            this.api.player.notifySeek(Math.floor(position));
        } else if (periodic || difference > 100) {
            this.api.player.notifyPosition(Math.floor(position));
        }
        this.lastReportedPosition = position;
    }

    onPositionSeek(position) {
        const manager = this.playbackManager;
        const current = manager._currentPlayer;
        if (current) {
            const duration = manager.duration();
            if (duration) manager.seekPercent((position * 10000) / duration * 100, current);
        }
        this.api.player.notifyPosition(Math.floor(position));
        this.lastReportedPosition = position;
        this.api.player.notifyRateChange(0.0);
    }

    clearTimers() {
        if (this.durationCheckInterval) clearInterval(this.durationCheckInterval);
        if (this.positionUpdateInterval) clearInterval(this.positionUpdateInterval);
        this.durationCheckInterval = null;
        this.positionUpdateInterval = null;
    }

    resetPlaybackTimers() {
        this.clearTimers();
        this.lastDuration = 0;
        this.lastReportedPosition = 0;
    }

    destroy() {
        if (this.destroyed) return;
        this.destroyed = true;
        this.clearTimers();
        this.unlisten(this.playerListeners);
        this.unlisten(this.listeners);
        this.attachedPlayer = null;
        for (const [signal, callback] of this.signals) signal.disconnect(callback);
        this.signals.length = 0;
        const callbacks = window.jmpInfo.settingsUpdate;
        const index = callbacks.indexOf(this.settingsCallback);
        if (index !== -1) callbacks.splice(index, 1);
    }
}

window._inputPlugin = inputPlugin;
})();
