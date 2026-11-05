const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { test } = require('node:test');

function deferred() {
    let resolve, reject;
    const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
    return { promise, resolve, reject };
}

function harness(deferLoad = false) {
    const calls = [], events = [], prompts = [], callbacks = [];
    const signal = { connect() {}, disconnect() {} };
    const player = new Proxy({}, {
        get(_target, key) {
            if (['playing', 'positionUpdate', 'finished', 'updateDuration', 'error', 'paused', 'bufferedRangesUpdated'].includes(key)) return signal;
            if (key === 'load') return (...args) => { calls.push([key, ...args.slice(0, -1)]); callbacks.push(args.at(-1)); if (!deferLoad) callbacks.at(-1)(); };
            return (...args) => calls.push([key, ...args]);
        }
    });
    const context = {
        window: { api: { player } },
        document: { body: { classList: { remove() {} } } },
        console: { error() {}, debug() {}, log() {} }, jmpInfo: { userAgent: 'test' }
    };
    for (const file of ['PlaybackIntent.js', 'PlaybackStreams.js', 'playbackErrorPrompt.js', 'mpvVideoPlayer.js']) {
        vm.runInNewContext(fs.readFileSync(path.join(__dirname, '../native', file), 'utf8'), context);
    }
    const instance = new context.window._mpvVideoPlayer({
        events: { trigger(_target, name, args) { events.push({ name, args }); } },
        loading: {}, appRouter: {}, globalize: {}, appHost: {}, appSettings: {},
        confirm(options) { const pending = deferred(); prompts.push({ ...pending, options }); return pending.promise; },
        dashboard: { default: { setBackdropTransparency() {} } }
    });
    return { instance, calls, events, prompts, callbacks };
}

test('a new completed load is not disturbed by an earlier prompt', async () => {
    const h = harness();
    const pending = h.instance.onError('old');
    h.instance.createMediaElement = () => Promise.resolve();
    await h.instance.play({ fullscreen: false, url: 'new', mediaSource: { MediaStreams: [] } });
    assert.equal(h.calls.filter(call => call[0] === 'load').length, 1);
    const before = h.calls.length;
    h.prompts[0].resolve();
    await pending;
    assert.equal(errors(h).length, 0);
    assert.deepEqual(h.calls.slice(before), []);
});

function errors(h) { return h.events.filter(event => event.name === 'error'); }

for (const rejected of [false, true]) {
    test(`current prompt ${rejected ? 'cancel' : 'retry'} emits existing payload once`, async () => {
        const h = harness();
        const pending = h.instance.onError('broken');
        assert.equal(h.calls.filter(call => call[0] === 'stop').length, 1);
        assert.equal(h.prompts[0].options.title, 'Playback Failed');
        h.prompts[0][rejected ? 'reject' : 'resolve']();
        await pending;
        assert.equal(errors(h).length, 1);
        const data = errors(h)[0].args[0];
        assert.equal(data.type, 'mediadecodeerror');
        assert.equal(data.streamInfo?.mediaSource.SupportsTranscoding, rejected ? false : undefined);
    });
}

for (const action of ['stop', 'play', 'destroy']) {
    for (const rejected of [false, true]) {
        test(`${action} invalidates pending ${rejected ? 'cancel' : 'retry'} synchronously`, async () => {
            const h = harness(action === 'play');
            const pending = h.instance.onError('old');
            let playing;
            if (action === 'play') {
                h.instance.createMediaElement = () => {};
                playing = h.instance.play({ fullscreen: false, url: 'new', mediaSource: { MediaStreams: [] } });
            } else h.instance[action]();
            const before = h.calls.length;
            h.prompts[0][rejected ? 'reject' : 'resolve']();
            await pending;
            assert.equal(errors(h).length, 0);
            assert.deepEqual(h.calls.slice(before), []);
            if (playing) { h.callbacks[0](); await playing; }
        });
    }
}

for (const newerFirst of [false, true]) for (const newerRejected of [false, true]) {
    test(`overlapping prompts only emit newest, newer first=${newerFirst} rejected=${newerRejected}`, async () => {
        const h = harness();
        const first = h.instance.onError('first');
        const second = h.instance.onError('second');
        const before = h.calls.length;
        const order = newerFirst ? [1, 0] : [0, 1];
        for (const index of order) h.prompts[index][(index ? newerRejected : !newerRejected) ? 'reject' : 'resolve']();
        await Promise.all([first, second]);
        assert.equal(errors(h).length, 1);
        assert.equal(errors(h)[0].args[0].streamInfo?.mediaSource.SupportsTranscoding, newerRejected ? false : undefined);
        assert.deepEqual(h.calls.slice(before), []);
    });
}

test('packaged scripts include the helper before the player', () => {
    const root = path.join(__dirname, '..');
    const paths = fs.readFileSync(path.join(root, 'src/system/NativeScriptPaths.h'), 'utf8');
    assert.ok(paths.indexOf('PlaybackIntent.js') < paths.indexOf('mpvVideoPlayer.js'));
    assert.ok(paths.indexOf('playbackErrorPrompt.js') >= 0);
    assert.ok(paths.indexOf('playbackErrorPrompt.js') < paths.indexOf('mpvVideoPlayer.js'));
    const component = fs.readFileSync(path.join(root, 'src/system/SystemComponent.cpp'), 'utf8');
    assert.match(component, /nativeScriptPaths\(\)/);
    const resources = fs.readFileSync(path.join(root, 'CMakeModules/GenerateQtResources.cmake'), 'utf8');
    assert.match(resources, /file\(GLOB_RECURSE _files "\$\{NATIVE_DIR\}\/\*"\)/);
});

test('pause and unpause retain their native commands', () => {
    const h = harness();
    h.instance.pause();
    h.instance.unpause();
    assert.deepEqual(h.calls.map(call => call[0]), ['pause', 'play']);
});

test('unclassified stop and unattributed error cleanup carry no false origin', async () => {
    const stop = harness();
    stop.instance.stop();
    assert.equal(stop.instance._intent.lastAction.origin, 'web_stop_unclassified');
    const error = harness();
    const pending = error.instance.onError('uncorrelated');
    assert.equal(error.instance._intent.lastAction.origin, 'web_cleanup_unattributed');
    assert.equal(error.instance._intent.lastAction.targetRequest, null);
    error.prompts[0].resolve();
    await pending;
    assert.equal(errors(error).length, 1);
});
