const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { test } = require('node:test');

function harness() {
    const calls = [], callbacks = [], events = [];
    const signal = { connect() {}, disconnect() {} };
    const player = new Proxy({}, { get(_target, key) {
        if (['playing', 'positionUpdate', 'finished', 'updateDuration', 'error', 'paused', 'bufferedRangesUpdated'].includes(key)) return signal;
        if (key === 'load') return (...args) => { calls.push(['load']); callbacks.push(args.at(-1)); };
        return (...args) => calls.push([key, ...args]);
    } });
    const context = { window: { api: { player } }, document: { body: { classList: { remove() {} } } },
        console: { error() {}, debug() {}, log() {} }, jmpInfo: { userAgent: 'test' } };
    for (const file of ['PlaybackIntent.js', 'PlaybackStreams.js', 'playbackErrorPrompt.js', 'mpvVideoPlayer.js']) {
        vm.runInNewContext(fs.readFileSync(path.join(__dirname, '../native', file), 'utf8'), context);
    }
    const instance = new context.window._mpvVideoPlayer({
        events: { trigger(_target, name) { events.push(name); } }, loading: {}, appRouter: {}, globalize: {},
        appHost: {}, appSettings: {}, confirm: () => new Promise(() => {}),
        dashboard: { default: { setBackdropTransparency() {} } }
    });
    instance.createMediaElement = () => {};
    return { instance, calls, callbacks, events, context };
}

function options(url) { return { fullscreen: false, url, mediaSource: { MediaStreams: [] } }; }

test('Stop invalidates local request before late load completion, while outer play settles', async () => {
    const h = harness();
    const play = h.instance.play(options('first'));
    const request = h.instance._intent.request;
    assert.ok(request > 0);
    await h.instance.stop();
    const before = h.calls.length;
    h.callbacks[0]('finished');
    assert.equal(await play, 'finished');
    assert.equal(h.instance._intent.request, null);
    assert.notEqual(h.instance._intent.lastLoadCallback, request);
    assert.deepEqual(h.calls.slice(before), []);
});

test('late first load callback cannot replace newer local play ticket', async () => {
    const h = harness();
    const a = h.instance.play(options('first'));
    const first = h.instance._intent.request;
    const b = h.instance.play(options('second'));
    const second = h.instance._intent.request;
    assert.notEqual(first, second);
    h.callbacks[0]('a');
    assert.equal(await a, 'a');
    assert.equal(h.instance._intent.request, second);
    assert.notEqual(h.instance._intent.lastLoadCallback, first);
    h.callbacks[1]('b');
    assert.equal(await b, 'b');
    assert.equal(h.instance._intent.lastLoadCallback, second);
});

test('uncorrelated cleanup remains a legacy Stop and clears local attribution', async () => {
    const h = harness();
    const play = h.instance.play(options('second'));
    h.instance.removeMediaDialog();
    assert.equal(h.calls.filter(call => call[0] === 'stop').length, 1);
    assert.equal(h.instance._intent.request, null);
    assert.equal(h.instance._intent.lastAction.origin, 'web_cleanup_unattributed');
    h.callbacks[0]();
    await play;
    assert.equal(h.instance._intent.request, null);
});

test('local tickets contain no payload or host-producer claim', () => {
    const h = harness();
    const intent = new h.context.window._PlaybackIntent();
    const play = intent.play();
    assert.deepEqual(Object.keys(play).sort(), ['commandId', 'operation', 'origin', 'targetRequest']);
    assert.equal(play.origin, 'web_play_unqualified');
    assert.equal(play.targetRequest, null);
    assert.equal(Object.hasOwn(h.context.window, 'intentProducerIdentity'), false);
});
