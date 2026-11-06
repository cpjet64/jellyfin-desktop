const { assert, test, deferred, request, fixture, item, source, tick } = require('./jellyfin_recovery_fixture.cjs');

async function prepare(f) {
    f.stages.item.resolve(item); await tick();
    f.stages.profile.resolve({}); await tick();
    f.stages.playback.resolve({ data: { MediaSources: [structuredClone(source)] } });
}

for (const kinds of [['terminal', 'settled'], ['settled', 'terminal']]) {
    test(`early ${kinds.join('/')} still resolves begin once`, async () => {
        const f = fixture();
        const key = request();
        f.player.play = value => {
            f.calls.play.push(value);
            for (const kind of kinds) assert.equal(f.manager.recoveryV1.observe({ ...key, kind }), true);
            assert.equal(f.manager.recoveryV1.observe({ ...key, kind: kinds[0] }), false);
            return f.stages.player.promise;
        };
        const pending = f.manager.recoveryV1.begin(key);
        await prepare(f);
        assert.equal((await pending).status, 'accepted');
        assert.equal(f.manager.recoveryV1.observe({ ...key, kind: 'terminal' }), false);
    });
}

for (const ending of ['cancel', 'throw']) {
    test(`early settlement followed by ${ending} completes begin`, async () => {
        const f = fixture();
        const key = request();
        f.player.play = () => {
            assert.equal(f.manager.recoveryV1.observe({ ...key, kind: 'terminal' }), true);
            assert.equal(f.manager.recoveryV1.observe({ ...key, kind: 'settled' }), true);
            if (ending === 'cancel') f.manager.recoveryV1.revoke(key);
            else throw Error('dispatch');
            return f.stages.player.promise;
        };
        const pending = f.manager.recoveryV1.begin(key);
        await prepare(f);
        assert.equal((await pending).status, ending === 'cancel' ? 'canceled' : 'rejected');
        assert.equal(f.manager.recoveryV1.observe({ ...key, kind: 'settled' }), false);
    });
}

test('correlated synchronous error is observed without touching ordinary player state', async () => {
    const f = fixture();
    const key = request();
    f.player.play = value => { f.calls.play.push(value); f.emit('error', { recovery: key }, { type: 'failure' });
        return f.stages.player.promise; };
    const pending = f.manager.recoveryV1.begin(key);
    await prepare(f);
    const result = await pending;
    assert.equal(result.status, 'accepted');
    assert.equal(result.dispatchError, true);
    assert.equal(result.attemptId, key.attemptId);
    assert.equal(f.calls.destroyed, 0);
    assert.equal(f.calls.reports.length, 0);
});

for (const behavior of ['resolve', 'reject', 'throw']) {
    test(`revoked old stop ${behavior} restores only its own subscription`, async () => {
        const f = fixture();
        const stopped = deferred();
        f.player.stop = () => { if (behavior === 'throw') throw Error('stop'); return stopped.promise; };
        f.player.streamInfo = { item, mediaSource: structuredClone(source), url: 'old://resource' };
        f.manager._currentPlayer = f.player;
        f.manager.getPlayerState = () => ({ PlayState: {}, NowPlayingItem: { Id: item.Id, ServerId: item.ServerId } });
        const pending = f.manager.recoveryV1.begin(request());
        await prepare(f); await tick();
        if (behavior !== 'throw') {
            assert.equal(f.manager.recoveryV1.revoke(request()), true);
            assert.doesNotThrow(() => f.emit('stopped', {}));
            if (behavior === 'resolve') stopped.resolve(); else stopped.reject(Error('stop'));
        }
        assert.equal((await pending).status, behavior === 'throw' ? 'rejected' : 'canceled');
        await tick();
        assert.doesNotThrow(() => f.emit('stopped', {}));
        assert.equal(f.calls.play.length, 0);
    });
}

test('late old stop cannot rebind over a newer ordinary transition', async () => {
    const f = fixture();
    const oldStop = deferred();
    let stopCount = 0;
    f.player.stop = () => ++stopCount === 1 ? oldStop.promise : Promise.resolve();
    f.player.streamInfo = { item, mediaSource: structuredClone(source), url: 'old://resource' };
    f.manager._currentPlayer = f.player;
    f.manager.getPlayerState = () => ({ PlayState: {}, NowPlayingItem: { Id: item.Id, ServerId: item.ServerId } });
    const pending = f.manager.recoveryV1.begin(request());
    await prepare(f); await tick();
    const ordinary = f.manager.play({ serverId: 'server-1', ids: ['item-1'], fullscreen: false });
    ordinary.catch(() => {});
    assert.equal((await pending).status, 'canceled');
    await tick(); await tick();
    const before = f.calls.bindings.length;
    oldStop.resolve(); await tick();
    assert.equal(f.calls.bindings.length, before);
});
