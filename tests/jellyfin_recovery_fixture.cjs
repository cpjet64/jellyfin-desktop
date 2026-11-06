const assert = require('node:assert/strict');
const test = require('node:test');
const { loadManager } = require('./jellyfin_recovery_harness.cjs');

function deferred() {
    let resolve, reject;
    const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
    return { promise, resolve, reject };
}

function request(attemptId = 'attempt-1') {
    return { hostSession: 'host-1', producerGeneration: 'document-1', attemptId, serverId: 'server-1',
        accountId: 'account-1', itemId: 'item-1', sourceId: 'source-1', positionTicks: 10000,
        state: 'paused', mediaType: 'Video', seekable: true, localPlayer: true, singleItem: true,
        live: false, remote: false, tracksVerified: true, timelineVerified: true,
        audioStreamIndex: 0, subtitleStreamIndex: -1 };
}

function fixture(boundary = { kind: 'w763-test-only', isTrusted: () => true, verifyEvidence: () => true }, interceptor = null, baseline = false) {
    const stages = { item: deferred(), profile: deferred(), playback: deferred(), player: deferred(), ordinary: deferred() };
    const calls = { play: [], requests: [], urls: [], stop: 0, events: [], reports: [], item: 0, destroyed: 0, bindings: [] };
    const listeners = new Map();
    const events = { on(target, type, callback) { calls.bindings.push(['on', target.name, type]); listeners.set(`${target.name}:${type}`, callback); },
        off(target, type) { calls.bindings.push(['off', target.name, type]); listeners.delete(`${target.name}:${type}`); },
        trigger: (...args) => calls.events.push(args) };
    const player = { name: 'fake', isLocalPlayer: true, recoveryEventCorrelation: 1, canPlayMediaType: () => true,
        getVolume: () => 50, setVolume() {}, getDeviceProfile: () => stages.profile.promise,
        play: value => { calls.play.push(value); return calls.play.length === 1 ? stages.player.promise : stages.ordinary.promise; },
        stop: () => { calls.stop++; return Promise.resolve(); }, destroy: () => { calls.destroyed++; },
        isMuted: () => false, paused: () => false, currentTime: () => 0, duration: () => 100 };
    const client = { serverId: () => 'server-1', getCurrentUserId: () => 'account-1',
        getItem: () => { calls.item++; return stages.item.promise; }, getSavedEndpointInfo: () => ({}),
        getCurrentUser: () => Promise.resolve({ Configuration: {} }),
        reportPlaybackStart: info => { calls.reports.push(['start', info]); return Promise.resolve(); },
        reportPlaybackProgress: info => { calls.reports.push(['progress', info]); return Promise.resolve(); },
        reportPlaybackStopped: info => { calls.reports.push(['stop', info]); return Promise.resolve(); },
        deviceId: () => 'device', accessToken: () => 'test-only',
        getUrl: value => { calls.urls.push(value); return `fresh://${calls.urls.length}/${value}`; } };
    class Queue { setPlaylist() {} setPlaylistState() {} getPlaylist() { return []; } getNextItemInfo() { return null; }
        reset() {} getRepeatMode() { return 'RepeatNone'; } getShuffleMode() { return 'Sorted'; }
        getCurrentPlaylistItemId() { return 'queue-1'; } }
    const fakes = { BaseItemKind: {}, ItemFilter: {}, ItemSortBy: {}, MediaType: {}, PlaybackErrorCode: {},
        getMediaInfoApi: () => ({ getPostedPlaybackInfo: value => { calls.requests.push(value); return stages.playback.promise; } }),
        merge: Object.assign, Screenfull: { isEnabled: false }, Events: events,
        datetime: {}, appSettings: { maxStreamingBitrate: () => 1000000, alwaysBurnInSubtitleWhenTranscoding: () => false },
        itemHelper: { isLocalItem: () => false },
        pluginManager: { ofType: type => type === 'player' ? [player] : (interceptor ? [interceptor] : []) },
        PlayQueueManager: Queue, userSettings: { enableCinemaMode: () => false }, globalize: { translate: value => value },
        loading: { show() {}, hide() {} }, appHost: { supports: () => false }, alert() {},
        PluginType: { MediaPlayer: 'player', PreplayIntercept: 'intercept' }, includesAny: () => false,
        getItems() {}, getItemBackdropImageUrl: () => null, PlayerEvent: {}, bindMediaSegmentManager() {},
        bindMediaSessionSubscriber() {}, AppFeature: {}, ServerConnections: { getApiClient: () => client },
        MediaError: {}, getMediaError() {}, toApi: value => value, bindSkipSegment: () => ({}),
        __recoveryTestBoundary: boundary };
    const manager = loadManager(fakes, baseline);
    const emit = (type, event, ...args) => listeners.get(`${player.name}:${type}`).call(player, event, ...args);
    return { manager, stages, calls, player, client, emit };
}

const item = { Id: 'item-1', ServerId: 'server-1', MediaType: 'Video', Type: 'Video', MediaStreams: [] };
const source = { Id: 'source-1', IsRemote: false, IsInfiniteStream: false, RequiresOpening: false,
    SupportsDirectStream: true, SupportsDirectPlay: false, RunTimeTicks: 1000000, Container: 'mkv',
    MediaStreams: [{ Index: 0, Type: 'Audio' }] };
const tick = () => new Promise(resolve => setImmediate(resolve));


module.exports = { assert, test, deferred, request, fixture, item, source, tick };
