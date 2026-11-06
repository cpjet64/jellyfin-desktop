const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { test } = require('node:test');

function harness() {
    const context = { window: {} };
    vm.runInNewContext(fs.readFileSync(path.join(__dirname, '../native/recoveryPlugin.js'), 'utf8'), context);
    let listener, readyListener, callback, request, plays = 0, calls = 0;
    const endpoint = { sessionId: 'host-session', ready: true, readyChanged: {
        connect(fn) { readyListener = fn; }, disconnect(fn) { if (readyListener === fn) readyListener = null; }
    }, revoked: {
        connect(fn) { listener = fn; }, disconnect(fn) { if (listener === fn) listener = null; }
    }, invoke(value, done) { ++calls; request = value; callback = done; } };
    const playbackManager = new Proxy({}, { get() { return () => { ++plays; }; } });
    const plugin = new context.window._recoveryPlugin({ playbackManager, ServerConnections: {}, events: {}, recoveryBridge: endpoint });
    return { context, plugin, endpoint, request: () => request, reply: value => callback(value),
        revoke: () => listener(), notifyReady: () => readyListener?.(true), calls: () => calls, plays: () => plays };
}

async function settled(promise) {
    let timer;
    try {
        return await Promise.race([promise.then(() => true), new Promise(resolve => { timer = setTimeout(() => resolve(false), 500); })]);
    } finally { clearTimeout(timer); }
}

test('attached bridge and injected dependencies never qualify unmodified producer', async () => {
    const h = harness();
    assert.equal(h.request().sessionId, 'host-session');
    assert.equal(h.request().commandId, '1');
    h.reply({ version: 1, commandId: '1', status: 'accepted' });
    await h.plugin.ready;
    const status = h.plugin.status();
    assert.equal(status.bridgeAttached, true);
    assert.equal(status.dependenciesAvailable, true);
    assert.equal(status.capability, 'unsupported');
    assert.equal(status.eligible, false);
    assert.equal(status.candidate, null);
    assert.equal(h.plays(), 0);
    h.plugin.destroy();
});

test('missing producer dependencies and bridge are reported independently', async () => {
    const h = harness();
    const absent = new h.context.window._recoveryPlugin();
    await absent.ready;
    assert.equal(absent.status().bridgeAttached, false);
    assert.equal(absent.status().dependenciesAvailable, false);
    assert.equal(absent.status().capability, 'unsupported');
    assert.equal(h.plays(), 0);
    absent.destroy();
    h.plugin.destroy();
});

for (const action of ['revoke', 'destroy']) {
    test(`late accepted handshake cannot reverse ${action}`, async () => {
        const h = harness();
        if (action === 'revoke') h.revoke(); else h.plugin.destroy();
        h.reply({ version: 1, commandId: '1', status: 'accepted' });
        await h.plugin.ready;
        assert.equal(h.plugin.status().bridgeAttached, false);
        assert.equal(h.plugin.status().eligible, false);
        assert.equal(h.plays(), 0);
    });
}

for (const action of ['revoke', 'destroy']) {
    test(`${action} settles readiness even if endpoint never responds`, async () => {
        const h = harness();
        if (action === 'revoke') h.revoke(); else h.plugin.destroy();
        assert.equal(await settled(h.plugin.ready), true);
        assert.equal(h.plugin.status().bridgeAttached, false);
    });
}

test('destroy settles readiness while apiPromise is unresolved', async () => {
    const h = harness();
    h.context.window.apiPromise = new Promise(() => {});
    const plugin = new h.context.window._recoveryPlugin();
    plugin.destroy();
    assert.equal(await settled(plugin.ready), true);
    assert.equal(plugin.status().bridgeAttached, false);
    h.plugin.destroy();
});

test('synchronous revocation while connecting prevents invoke and tolerates disconnect failure', async () => {
    const h = harness();
    let calls = 0;
    const endpoint = { sessionId: 'host-session', ready: true, readyChanged: { connect() {}, disconnect() {} }, revoked: {
        connect(fn) { fn(); }, disconnect() { throw new Error('private'); }
    }, invoke() { ++calls; } };
    const plugin = new h.context.window._recoveryPlugin({ recoveryBridge: endpoint });
    assert.equal(await settled(plugin.ready), true);
    assert.equal(plugin.status().bridgeAttached, false);
    assert.equal(calls, 0);
    assert.doesNotThrow(() => plugin.destroy());
    h.plugin.destroy();
});

test('pending endpoint sends exactly one handshake after ready and never after revoke', async () => {
    const h = harness();
    h.plugin.destroy();
    let readySignal, revokeSignal, calls = 0, callback;
    const endpoint = { sessionId: 'next', ready: false,
        readyChanged: { connect(fn) { readySignal = fn; }, disconnect() {} },
        revoked: { connect(fn) { revokeSignal = fn; }, disconnect() {} },
        invoke(_request, done) { ++calls; callback = done; } };
    const pending = new h.context.window._recoveryPlugin({ recoveryBridge: endpoint });
    assert.equal(calls, 0);
    readySignal(true); readySignal(true);
    assert.equal(calls, 1);
    revokeSignal();
    callback({ version: 1, commandId: '1', status: 'accepted' });
    await pending.ready;
    assert.equal(pending.status().bridgeAttached, false);
    pending.destroy();
});

test('wrong or extended receipt fails without making producer eligible', async () => {
    for (const receipt of [{ version: 1, commandId: '2', status: 'accepted' },
        { version: 1, commandId: '1', status: 'accepted', producer: 'forged' },
        { version: 1, commandId: '1', status: 'revoked' },
        { version: 1, commandId: '1', status: 'duplicate' }]) {
        const h = harness();
        h.reply(receipt);
        await h.plugin.ready;
        assert.equal(h.plugin.status().bridgeAttached, false);
        assert.equal(h.plugin.status().capability, 'unsupported');
        assert.equal(h.plays(), 0);
        h.plugin.destroy();
    }
});

test('a first rejected callback cannot be overwritten by a later accepted one', async () => {
    const h = harness();
    h.reply({ version: 1, commandId: '1', status: 'rejected' });
    await h.plugin.ready;
    h.reply({ version: 1, commandId: '1', status: 'accepted' });
    assert.equal(h.plugin.status().bridgeAttached, false);
    assert.equal(h.plays(), 0);
    h.plugin.destroy();
});

test('explicit candidate context is bounded, account-bound and never replayable', () => {
    const h = harness();
    const evaluate = h.context.window._recoveryPlugin.evaluateCandidate;
    const account = { serverId: 'server', accountId: 'account' };
    const candidate = { attemptId: 'attempt-a', serverId: 'server', accountId: 'account', itemId: 'item', mediaSourceId: null };
    const first = evaluate(candidate, account);
    const second = evaluate({ ...candidate, attemptId: 'attempt-b' }, account);
    assert.notEqual(first.attemptId, second.attemptId);
    assert.equal(first.sourcePosition, null);
    assert.equal(first.mediaSourceId, null);
    assert.equal(first.eligible, false);
    for (const invalid of [{ ...candidate, url: 'https://example.invalid' },
        { ...candidate, itemId: 'https://secret.invalid' }, { ...candidate, token: 'credential' },
        { ...candidate, itemId: 'x'.repeat(8193) }, { ...candidate, accountId: 'other' }]) {
        assert.equal(evaluate(invalid, account), null);
    }
    assert.equal(evaluate(candidate, { ...account, accountId: 'other' }), null);
    assert.equal(h.plays(), 0);
    h.plugin.destroy();
});
