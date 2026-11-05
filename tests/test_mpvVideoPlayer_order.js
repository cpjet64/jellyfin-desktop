const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { test } = require('node:test');

function fixture(existing = false) {
    const calls = [], callbacks = [];
    let container = existing ? {} : null;
    const signal = { connect() {}, disconnect() {} };
    const bridge = new Proxy({}, { get(target, key) {
        if (target[key]) return target[key];
        if (key === 'load') return (...args) => {
            calls.push({ name: 'load', args: args.slice(0, -1) });
            callbacks.push(args.at(-1));
        };
        if (['playing', 'positionUpdate', 'finished', 'updateDuration', 'error', 'paused',
            'bufferedRangesUpdated', 'buffering', 'canceled', 'stopped', 'stateChanged',
            'videoPlaybackActive', 'windowVisible', 'onVideoRecangleChanged', 'onMetaData'].includes(key)) return signal;
        return (...args) => calls.push({ name: key, args });
    } });
    const body = { classList: { add() {}, remove() {} }, insertBefore(element) { element.parentNode = body; container = element; },
        removeChild() { container = null; } };
    const document = { body, querySelector() { return container; },
        createElement() { return { classList: { add() {} }, style: {} }; } };
    const context = { window: { api: { player: bridge } }, document, jmpInfo: { userAgent: 'test' },
        console: { error() {}, debug() {}, log() {} } };
    for (const file of ['PlaybackIntent.js', 'PlaybackStreams.js', 'playbackErrorPrompt.js', 'mpvVideoPlayer.js']) {
        vm.runInNewContext(fs.readFileSync(path.join(__dirname, '../native', file), 'utf8'), context);
    }
    const instance = new context.window._mpvVideoPlayer({ events: { trigger() {} },
        loading: { show() {} }, appRouter: {}, globalize: {}, appHost: {}, appSettings: {},
        confirm() {}, dashboard: { default: { setBackdropTransparency() {} } } });
    return { instance, calls, callbacks, document, bridge };
}

function options(name, ticks) {
    return { url: name, fullscreen: false, playerStartPositionTicks: ticks,
        item: { Id: name }, mediaSource: { MediaStreams: [], DefaultAudioStreamIndex: -1 } };
}

function commands(h) { return h.calls.filter(call => ['load', 'stop'].includes(call.name)); }

for (const existing of [false, true]) {
    for (const action of ['stop', 'destroy']) {
        test(`${existing ? 'existing' : 'new'} container: play then ${action} stays ordered`, async () => {
            const h = fixture(existing);
            const play = h.instance.play(options('A', 10000));
            h.instance[action]();
            assert.deepEqual(commands(h).map(call => call.name), ['load', 'stop']);
            await Promise.resolve();
            assert.deepEqual(commands(h).map(call => call.name), ['load', 'stop']);
            h.callbacks[0]('A done');
            assert.equal(await play, 'A done');
            assert.deepEqual(commands(h).map(call => call.name), ['load', 'stop']);
        });
    }

    test(`${existing ? 'existing' : 'new'} container: consecutive plays preserve options and callbacks`, async () => {
        const h = fixture(existing), a = options('A', 10000), b = options('B', 30000);
        const first = h.instance.play(a), second = h.instance.play(b);
        const loads = commands(h);
        assert.deepEqual(loads.map(call => call.args[0]), ['A', 'B']);
        assert.equal(loads[0].args[1].startMilliseconds, 1);
        assert.equal(loads[1].args[1].startMilliseconds, 3);
        assert.equal(loads[0].args[2].metadata, a.item);
        assert.equal(loads[1].args[2].metadata, b.item);
        h.callbacks[1]('B done'); h.callbacks[0]('A done');
        assert.equal(await second, 'B done'); assert.equal(await first, 'A done');
        assert.deepEqual(commands(h).map(call => call.args[0]), ['A', 'B']);
        assert.equal(h.instance.currentSrc(), 'B');
        assert.equal(h.instance._currentPlayOptions, b);
    });

    test(`${existing ? 'existing' : 'new'} container: play stop play stays ordered`, async () => {
        const h = fixture(existing);
        const first = h.instance.play(options('A', 0));
        h.instance.stop();
        const b = options('B', 0);
        const second = h.instance.play(b);
        assert.deepEqual(commands(h).map(call => call.name), ['load', 'stop', 'load']);
        await Promise.resolve();
        assert.deepEqual(commands(h).map(call => call.name), ['load', 'stop', 'load']);
        h.callbacks[1]('B'); h.callbacks[0]('A');
        assert.equal(await first, 'A'); assert.equal(await second, 'B');
        assert.deepEqual(commands(h).map(call => call.name), ['load', 'stop', 'load']);
        assert.equal(h.instance.currentSrc(), 'B');
        assert.equal(h.instance._currentPlayOptions, b);
    });
}

test('setup and native load exceptions reject the outer play promise', async () => {
    const setup = fixture();
    setup.document.querySelector = () => { throw new Error('setup failure'); };
    await assert.rejects(setup.instance.play(options('A', 0)), /setup failure/);
    const load = fixture(true);
    load.bridge.load = () => { throw new Error('load failure'); };
    await assert.rejects(load.instance.play(options('A', 0)), /load failure/);
});

test('a late load callback cannot adopt a newer local request', async () => {
    const h = fixture(true);
    const first = h.instance.play(options('A', 0));
    const a = h.instance._intent.request;
    const second = h.instance.play(options('B', 0));
    const b = h.instance._intent.request;
    assert.ok(Number.isSafeInteger(a) && b > a);
    h.callbacks[0]('A');
    assert.equal(await first, 'A');
    assert.equal(h.instance._intent.lastLoadCallback, null);
    h.callbacks[1]('B');
    assert.equal(await second, 'B');
    assert.equal(h.instance._intent.lastLoadCallback, b);
});

test('stop invalidates a pending local callback without changing its promise', async () => {
    const h = fixture(true);
    const play = h.instance.play(options('A', 0));
    h.instance.stop();
    assert.equal(h.instance._intent.request, null);
    h.callbacks[0]('done');
    assert.equal(await play, 'done');
    assert.equal(h.instance._intent.lastLoadCallback, null);
    assert.deepEqual(commands(h).map(call => call.name), ['load', 'stop']);
});

test('local tickets do not retain playback URLs or item metadata', async () => {
    const h = fixture(true);
    const play = h.instance.play(options('https://example.invalid/private?token=secret', 0));
    const snapshot = JSON.stringify(h.instance._intent);
    assert.doesNotMatch(snapshot, /https:|token|secret|example/);
    h.callbacks[0]('done');
    assert.equal(await play, 'done');
});
