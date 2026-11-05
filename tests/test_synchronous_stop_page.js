const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { test } = require('node:test');

function harness() {
    const calls = [], callbacks = [], listeners = {}, signals = {};
    let dialog;
    const signalNames = ['playing', 'positionUpdate', 'finished', 'updateDuration', 'error', 'paused',
        'bufferedRangesUpdated', 'buffering', 'canceled', 'stopped', 'stateChanged',
        'videoPlaybackActive', 'windowVisible', 'onVideoRecangleChanged', 'onMetaData'];
    for (const name of signalNames) {
        signals[name] = { handlers: [], connect(fn) { this.handlers.push(fn); }, disconnect() {} };
    }
    const bridge = new Proxy(signals, { get(target, key) {
        if (key in target) return target[key];
        return (...args) => {
            calls.push(key);
            if (key === 'load') callbacks.push(args.at(-1));
        };
    } });
    const body = { classList: { add() {}, remove() {} }, insertBefore(element) { dialog = element; element.parentNode = body; },
        removeChild() { dialog = null; } };
    const document = { body, querySelector() { return dialog; },
        createElement() { return { classList: { add() {} }, style: {} }; } };
    const window = { fixturePlayer: bridge, addEventListener(name, fn) { listeners[name] = fn; } };
    const context = vm.createContext({ window, document, console, jmpInfo: { userAgent: 'fixture' } });
    for (const file of ['native/playbackErrorPrompt.js', 'native/mpvVideoPlayer.js', 'tests/synchronous_stop_page.js']) {
        vm.runInContext(fs.readFileSync(path.join(__dirname, '..', file), 'utf8'), context);
    }
    return { window, calls, callbacks, listeners, signals };
}

test('normal playing signals and adjacent play-stop use the real page adapter', async () => {
    const h = harness();
    assert.equal(h.window.fixtureStartA('a'), 'sent');
    h.callbacks.shift()(true);
    await Promise.resolve();
    await Promise.resolve();
    for (const handler of h.signals.playing.handlers) handler();
    assert.equal(h.window.fixturePlayBStop('b'), 'sent');
    h.callbacks.shift()(true);
    await Promise.resolve();
    await Promise.resolve();
    const state = JSON.parse(h.window.fixtureDispatchState());
    assert.deepEqual(state.actions, ['playA', 'playB', 'stop']);
    assert.deepEqual(state.errors, []);
    assert.equal(state.overflow, false);
    assert.equal(state.pending, 0);
    assert.deepEqual(h.calls.filter(value => ['load', 'stop'].includes(value)), ['load', 'load', 'stop']);
    for (const name of ['setVolume', 'setPlaybackRate', 'notifyRateChange', 'setVideoRectangle']) {
        assert.ok(h.calls.includes(name), name);
    }
});

test('page failures are bounded and overflow remains visible', () => {
    const h = harness();
    for (let i = 0; i < 17; ++i) h.listeners.error({ message: 'x'.repeat(1000) });
    const state = JSON.parse(h.window.fixtureDispatchState());
    assert.equal(state.errors.length, 16);
    assert.ok(state.errors.every(value => value.length <= 512));
    assert.equal(state.overflow, true);
});
