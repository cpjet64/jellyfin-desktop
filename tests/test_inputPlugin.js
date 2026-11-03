const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { test } = require('node:test');

// Mirrors jellyfin-web's events.ts: off removes only the supplied callback.
const events = {
    on(target, type, fn) { ((target._callbacks ||= {})[type] ||= []).push(fn); },
    off(target, type, fn) {
        const callbacks = (target._callbacks ||= {})[type] ||= [];
        const index = callbacks.indexOf(fn);
        if (index !== -1) callbacks.splice(index, 1);
    },
    trigger(target, type, args = []) {
        for (const fn of [...((target._callbacks || {})[type] || [])]) fn.apply(target, [{ type }, ...args]);
    }
};

function signal() {
    const callbacks = [];
    return {
        connect(fn) { callbacks.push(fn); },
        disconnect(fn) { const i = callbacks.indexOf(fn); if (i !== -1) callbacks.splice(i, 1); },
        emit(...args) { callbacks.slice().forEach(fn => fn(...args)); },
        count() { return callbacks.length; }
    };
}

function harness() {
    let resolveApi;
    const apiPromise = new Promise(resolve => { resolveApi = resolve; });
    const calls = [];
    const input = Object.fromEntries(['hostInput', 'volumeChanged', 'rateChanged', 'positionSeek'].map(key => [key, signal()]));
    const rateSignal = signal();
    const player = new Proxy({ playbackRateChanged: rateSignal }, {
        get(target, key) { return target[key] || ((...args) => calls.push([key, ...args])); }
    });
    const api = { input, player, system: { hello: (...args) => calls.push(['hello', ...args]) } };
    const manager = {
        _currentPlayer: null,
        _playQueueManager: { getPlaylist: () => [1, 2], getCurrentPlaylistIndex: () => 0 },
        getPlayerState: () => ({ NowPlayingItem: { MediaType: 'Video' } }),
        currentTime: () => 100, duration: () => 100000,
        getQueueShuffleMode: () => 'Shuffle', getRepeatMode: () => 'RepeatNone'
    };
    const settingsUpdate = [];
    const timers = new Map();
    let nextTimer = 0;
    const context = {
        window: { apiPromise, jmpInfo: { settings: { main: { fullscreen: false } }, settingsUpdate } },
        console, setInterval(fn) { const id = ++nextTimer; timers.set(id, fn); return id; },
        clearInterval(id) { timers.delete(id); }
    };
    vm.runInNewContext(fs.readFileSync(path.join(__dirname, '../native/inputPlugin.js'), 'utf8'), context);
    const plugin = new context.window._inputPlugin({ events, playbackManager: manager, inputManager: { handleCommand() {} } });
    return { plugin, api, input, manager, calls, settingsUpdate, timers, resolveApi, rateSignal };
}

test('switching A to B to A removes only owned callbacks and clears timers', async () => {
    const h = harness();
    h.resolveApi(h.api);
    await new Promise(resolve => setImmediate(resolve));
    const a = { getVolume: () => 50, getPlaybackRate: () => 1 };
    const b = { getVolume: () => 40, getPlaybackRate: () => 1 };
    let unrelated = 0;
    let unrelatedStop = 0;
    events.on(a, 'playing', () => { unrelated++; });
    events.on(h.manager, 'playbackstop', () => { unrelatedStop++; });
    const start = player => { h.manager._currentPlayer = player; events.trigger(h.manager, 'playbackstart', [player]); };
    start(a);
    events.trigger(a, 'playing');
    assert.equal(h.timers.size, 2);
    start(b);
    assert.equal(h.timers.size, 0);
    start(a);
    h.calls.length = 0;
    events.trigger(a, 'playing');
    assert.equal(unrelated, 2);
    assert.equal(h.calls.filter(call => call[0] === 'notifyPlaybackState').length, 1);
    assert.equal(h.timers.size, 2);
    h.plugin.destroy();
    h.plugin.destroy();
    assert.equal(h.timers.size, 0);
    assert.equal(h.settingsUpdate.length, 0);
    assert.equal(h.input.hostInput.count(), 0);
    assert.equal(h.rateSignal.count(), 0);
    for (const item of Object.values(h.input)) assert.equal(item.count(), 0);
    h.calls.length = 0;
    events.trigger(a, 'playing');
    events.trigger(h.manager, 'playbackstart', [a]);
    events.trigger(h.manager, 'playbackstop', [{}]);
    assert.equal(unrelated, 3);
    assert.equal(unrelatedStop, 1);
    assert.equal(h.calls.length, 0);
});

test('destroy before API resolves attaches no listeners or signals', async () => {
    const h = harness();
    h.plugin.destroy();
    h.resolveApi(h.api);
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(h.input.hostInput.count(), 0);
    assert.equal(h.rateSignal.count(), 0);
    assert.equal(h.settingsUpdate.length, 0);
    assert.equal(h.calls.length, 0);
    assert.equal(Object.keys(h.manager._callbacks || {}).length, 0);
});
