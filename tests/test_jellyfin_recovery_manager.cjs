const { assert, test, deferred, request, fixture, item, source, tick } = require('./jellyfin_recovery_fixture.cjs');

test('actual patched manager dispatches freshly negotiated request with full correlation', async () => {
    const { manager, stages, calls } = fixture();
    const pending = manager.recoveryV1.begin(request());
    stages.item.resolve(item);
    await tick();
    stages.profile.resolve({});
    await tick();
    stages.playback.resolve({ data: { MediaSources: [structuredClone(source)] } });
    const result = await pending;
    assert.equal(result.status, 'accepted');
    assert.equal(calls.play.length, 1);
    assert.equal(calls.play[0].recovery.attemptId, 'attempt-1');
    assert.equal(calls.play[0].recovery.hostSession, 'host-1');
    assert.equal(calls.play[0].recovery.producerGeneration, 'document-1');
    assert.equal(calls.requests[0].playbackInfoDto.StartTimeTicks, 10000);
    assert.ok(calls.play[0].url.startsWith('fresh://'));
    assert.equal(Object.hasOwn(calls.play[0].item.playOptions, 'attemptId'), false);
    assert.equal(Object.hasOwn(calls.requests[0].playbackInfoDto, 'attemptId'), false);
    assert.equal(Object.hasOwn(calls.requests[0].playbackInfoDto, 'hostSession'), false);
});

for (const stopStage of ['before-item', 'item', 'profile', 'playback']) {
    test(`actual manager Stop cancels recovery at ${stopStage}`, async () => {
        const { manager, stages, calls } = fixture();
        const pending = manager.recoveryV1.begin(request());
        if (stopStage !== 'before-item') await tick();
        if (['profile', 'playback'].includes(stopStage)) { stages.item.resolve(item); await tick(); }
        if (stopStage === 'playback') { stages.profile.resolve({}); await tick(); }
        await manager.stop();
        assert.equal((await pending).status, 'canceled');
        stages.item.resolve(item);
        stages.profile.resolve({});
        stages.playback.resolve({ data: { MediaSources: [structuredClone(source)] } });
        await tick();
        assert.equal(calls.play.length, 0);
        assert.equal(calls.events.length, 0);
    });
}

test('ordinary play supersedes pending recovery for same item', async () => {
    const { manager, stages, calls } = fixture();
    const pending = manager.recoveryV1.begin(request());
    await tick();
    const ordinary = manager.play({ serverId: 'server-1', ids: ['item-1'], fullscreen: false });
    ordinary.catch(() => {});
    assert.equal((await pending).status, 'canceled');
    assert.equal(calls.item, 2);
    stages.item.resolve(item);
    await tick();
    assert.equal(calls.play.filter(value => value.recovery).length, 0);
});

test('accepted attempt stays unsettled and stale completion cannot start or report', async () => {
    const { manager, stages, calls } = fixture();
    const first = request();
    const pending = manager.recoveryV1.begin(first);
    stages.item.resolve(item); await tick();
    stages.profile.resolve({}); await tick();
    stages.playback.resolve({ data: { MediaSources: [structuredClone(source)] } });
    assert.equal((await pending).status, 'accepted');
    await manager.stop();
    assert.equal((await manager.recoveryV1.begin(request('attempt-2'))).status, 'rejected');
    stages.player.resolve();
    await tick();
    assert.equal(calls.events.length, 0);
    const key = { hostSession: first.hostSession, producerGeneration: first.producerGeneration, attemptId: first.attemptId };
    assert.equal(manager.recoveryV1.observe({ ...key, kind: 'terminal' }), true);
    assert.equal(manager.recoveryV1.observe({ ...key, producerGeneration: 'other', kind: 'settled' }), false);
    assert.equal(manager.recoveryV1.observe({ ...key, kind: 'terminal' }), false);
    assert.equal(manager.recoveryV1.observe({ ...key, kind: 'settled' }), true);
});

test('unrelated producer context cannot revoke and a fresh context is independent', async () => {
    const old = fixture();
    const fresh = fixture();
    const first = old.manager.recoveryV1.begin(request());
    const context = { hostSession: 'host-1', producerGeneration: 'document-1' };
    assert.equal(old.manager.recoveryV1.revoke({ ...context, producerGeneration: 'other' }), false);
    assert.equal(old.manager.recoveryV1.revoke(context), true);
    assert.equal((await first).status, 'canceled');
    const next = { ...request('attempt-2'), hostSession: 'host-2', producerGeneration: 'document-2' };
    const pending = fresh.manager.recoveryV1.begin(next);
    fresh.stages.item.resolve(item); await tick();
    fresh.stages.profile.resolve({}); await tick();
    fresh.stages.playback.resolve({ data: { MediaSources: [structuredClone(source)] } });
    assert.equal((await pending).status, 'accepted');
    assert.equal(fresh.calls.play[0].recovery.hostSession, 'host-2');
    assert.equal(fresh.calls.play[0].recovery.producerGeneration, 'document-2');
});

for (const mutation of [
    value => { value.Id = 'other'; },
    value => { value.IsRemote = true; },
    value => { value.IsInfiniteStream = true; },
    value => { value.RequiresOpening = true; },
    value => { value.SupportsDirectStream = false; },
    value => { value.RunTimeTicks = 10000; },
    value => { value.MediaStreams = []; }
]) {
    test('unsupported fresh source is rejected before player dispatch', async () => {
        const { manager, stages, calls } = fixture();
        const pending = manager.recoveryV1.begin(request());
        stages.item.resolve(item); await tick();
        stages.profile.resolve({}); await tick();
        const changed = structuredClone(source);
        mutation(changed);
        stages.playback.resolve({ data: { MediaSources: [changed] } });
        assert.equal((await pending).status, 'rejected');
        assert.equal(calls.play.length, 0);
    });
}

test('synchronous reentrant Stop cancels outcome but retains dispatch ownership', async () => {
    const { manager, stages, calls, player } = fixture();
    player.play = value => { calls.play.push(value); manager.stop(); return stages.player.promise; };
    const first = request();
    const pending = manager.recoveryV1.begin(first);
    stages.item.resolve(item); await tick();
    stages.profile.resolve({}); await tick();
    stages.playback.resolve({ data: { MediaSources: [structuredClone(source)] } });
    assert.equal((await pending).status, 'canceled');
    assert.equal((await manager.recoveryV1.begin(request('attempt-2'))).status, 'rejected');
    stages.player.resolve(); await tick();
    assert.equal(calls.events.length, 0);
    const key = { hostSession: first.hostSession, producerGeneration: first.producerGeneration, attemptId: first.attemptId };
    assert.equal(manager.recoveryV1.observe({ ...key, kind: 'terminal' }), true);
    assert.equal(manager.recoveryV1.observe({ ...key, kind: 'settled' }), true);
});

test('request is snapshotted before asynchronous preparation', async () => {
    const { manager, stages, calls } = fixture();
    const input = request();
    const pending = manager.recoveryV1.begin(input);
    input.positionTicks = 90000;
    input.attemptId = 'changed';
    input.sourceId = 'changed';
    stages.item.resolve(item); await tick();
    input.accountId = 'changed';
    stages.profile.resolve({}); await tick();
    stages.playback.resolve({ data: { MediaSources: [structuredClone(source)] } });
    assert.equal((await pending).attemptId, 'attempt-1');
    assert.equal(calls.requests[0].playbackInfoDto.StartTimeTicks, 10000);
    assert.equal(calls.play[0].recovery.attemptId, 'attempt-1');
});

test('untagged ordinary callback remains enabled while recovery is unsettled', async () => {
    const { manager, stages, calls, emit } = fixture();
    manager.getPlayerState = () => ({ PlayState: {}, NowPlayingItem: { Id: item.Id, ServerId: item.ServerId } });
    const pending = manager.recoveryV1.begin(request());
    stages.item.resolve(item); await tick();
    stages.profile.resolve({}); await tick();
    stages.playback.resolve({ data: { MediaSources: [structuredClone(source)] } });
    assert.equal((await pending).status, 'accepted');
    stages.player.resolve(); await tick();
    const before = calls.reports.length;
    emit('timeupdate', {});
    assert.equal(calls.reports.length, before + 1);
    assert.equal(calls.reports.at(-1)[0], 'progress');
});

test('synchronous player throw rejects once and retains uncertain dispatch', async () => {
    const { manager, stages, player } = fixture();
    player.play = () => { throw new Error('dispatch failure'); };
    const first = request();
    const pending = manager.recoveryV1.begin(first);
    stages.item.resolve(item); await tick();
    stages.profile.resolve({}); await tick();
    stages.playback.resolve({ data: { MediaSources: [structuredClone(source)] } });
    assert.equal((await pending).status, 'rejected');
    assert.equal((await manager.recoveryV1.begin(request('attempt-2'))).status, 'rejected');
    const key = { hostSession: first.hostSession, producerGeneration: first.producerGeneration, attemptId: first.attemptId };
    assert.equal(manager.recoveryV1.observe({ ...key, kind: 'terminal' }), true);
    assert.equal(manager.recoveryV1.observe({ ...key, kind: 'settled' }), true);
});

test('Stop while preplay interceptor is pending prevents later preparation', async () => {
    const blocked = deferred();
    const { manager, stages, calls } = fixture(undefined, { intercept: () => blocked.promise });
    const pending = manager.recoveryV1.begin(request());
    stages.item.resolve(item); await tick();
    await manager.stop();
    assert.equal((await pending).status, 'canceled');
    blocked.resolve(); await tick();
    assert.equal(calls.requests.length, 0);
    assert.equal(calls.play.length, 0);
});

test('Stop while old-player stop is pending prevents stale dispatch and reporting', async () => {
    const { manager, stages, calls, player } = fixture();
    const oldStop = deferred();
    player.stop = () => { calls.stop++; return oldStop.promise; };
    player.streamInfo = { item, mediaSource: structuredClone(source), url: 'old://resource' };
    manager._currentPlayer = player;
    manager.getPlayerState = () => ({ PlayState: {}, NowPlayingItem: { Id: item.Id, ServerId: item.ServerId } });
    const pending = manager.recoveryV1.begin(request());
    stages.item.resolve(item); await tick();
    stages.profile.resolve({}); await tick();
    stages.playback.resolve({ data: { MediaSources: [structuredClone(source)] } });
    await tick();
    assert.equal(calls.stop, 1);
    const stopping = manager.stop();
    assert.equal((await pending).status, 'canceled');
    oldStop.resolve();
    await stopping; await tick();
    assert.equal(calls.play.length, 0);
    assert.equal(calls.reports.length, 0);
});

test('second same-item recovery supersedes the first while preparation is pending', async () => {
    const { manager, stages, calls } = fixture();
    const first = manager.recoveryV1.begin(request());
    await tick();
    const second = manager.recoveryV1.begin(request('attempt-2'));
    assert.equal((await first).status, 'canceled');
    stages.item.resolve(item); await tick();
    stages.profile.resolve({}); await tick();
    stages.playback.resolve({ data: { MediaSources: [structuredClone(source)] } });
    assert.equal((await second).status, 'accepted');
    assert.equal(calls.play.length, 1);
    assert.equal(calls.play[0].recovery.attemptId, 'attempt-2');
});

test('missing player correlation capability rejects before negotiation or dispatch', async () => {
    const { manager, stages, calls, player } = fixture();
    delete player.recoveryEventCorrelation;
    const pending = manager.recoveryV1.begin(request());
    stages.item.resolve(item);
    assert.equal((await pending).status, 'rejected');
    assert.equal(calls.requests.length, 0);
    assert.equal(calls.play.length, 0);
});

test('provisional same-URL bridge attachment cannot act as a test binding', async () => {
    const provisional = { kind: 'provisional', observationReady: true, url: 'https://same.example/',
        isTrusted: () => true };
    const { manager, calls } = fixture(provisional);
    const result = await manager.recoveryV1.begin(request());
    assert.equal(result.status, 'rejected');
    assert.equal(calls.item, 0);
    assert.equal(calls.play.length, 0);
});

test('unbound manager and mismatched account or server fail before lookup', async () => {
    const unbound = fixture(null);
    assert.equal((await unbound.manager.recoveryV1.begin(request())).status, 'rejected');
    for (const field of ['serverId', 'getCurrentUserId']) {
        const candidate = fixture();
        candidate.client[field] = () => 'different';
        const result = await candidate.manager.recoveryV1.begin(request());
        assert.equal(result.status, 'rejected');
        assert.equal(candidate.calls.item, 0);
    }
});

test('fresh external subtitle URL is derived from new source and never supplied by request', async () => {
    const { manager, stages, calls } = fixture();
    const desired = { ...request(), subtitleStreamIndex: 2 };
    const pending = manager.recoveryV1.begin(desired);
    stages.item.resolve(item); await tick();
    stages.profile.resolve({}); await tick();
    const fresh = structuredClone(source);
    fresh.MediaStreams.push({ Index: 2, Type: 'Subtitle', DeliveryMethod: 'External',
        DeliveryUrl: '/new-subtitle', IsExternalUrl: false, Codec: 'srt' });
    stages.playback.resolve({ data: { MediaSources: [fresh] } });
    assert.equal((await pending).status, 'accepted');
    assert.ok(calls.play[0].textTracks[0].url.includes('/new-subtitle'));
    assert.equal(calls.requests[0].playbackInfoDto.SubtitleStreamIndex, 2);
    assert.equal(Object.hasOwn(calls.requests[0].playbackInfoDto, 'PlaySessionId'), false);
});

test('test boundary must validate both matching settlement observations', async () => {
    let allow = false;
    const { manager, stages } = fixture({ kind: 'w763-test-only', isTrusted: () => true,
        verifyEvidence: () => allow });
    const first = request();
    const pending = manager.recoveryV1.begin(first);
    stages.item.resolve(item); await tick();
    stages.profile.resolve({}); await tick();
    stages.playback.resolve({ data: { MediaSources: [structuredClone(source)] } });
    assert.equal((await pending).status, 'accepted');
    const key = { hostSession: first.hostSession, producerGeneration: first.producerGeneration, attemptId: first.attemptId };
    assert.equal(manager.recoveryV1.observe({ ...key, kind: 'terminal' }), false);
    allow = true;
    assert.equal(manager.recoveryV1.observe({ ...key, kind: 'terminal' }), true);
    assert.equal(manager.recoveryV1.observe({ ...key, kind: 'settled' }), true);
});

test('new ordinary playback handles its events while stale tagged events are ignored', async () => {
    const { manager, stages, calls, player, emit } = fixture();
    manager.getPlayerState = () => ({ PlayState: {}, NowPlayingItem: { Id: item.Id, ServerId: item.ServerId } });
    const first = request();
    const pending = manager.recoveryV1.begin(first);
    stages.item.resolve(item); await tick();
    stages.profile.resolve({}); await tick();
    stages.playback.resolve({ data: { MediaSources: [structuredClone(source)] } });
    assert.equal((await pending).status, 'accepted');
    const ordinary = manager.play({ serverId: 'server-1', ids: ['item-1'], fullscreen: false });
    await tick(); await tick();
    assert.equal(calls.play.length, 2);
    stages.ordinary.resolve();
    await ordinary;
    await tick();
    const reports = calls.reports.length;
    const events = calls.events.length;
    stages.player.resolve();
    await tick();
    assert.equal(calls.events.length, events);
    assert.equal(calls.reports.length, reports);
    const tagged = { recovery: { hostSession: first.hostSession, producerGeneration: first.producerGeneration,
        attemptId: first.attemptId } };
    emit('timeupdate', tagged);
    emit('error', tagged, { type: 'failure' });
    emit('stopped', tagged);
    assert.equal(calls.reports.length, reports);
    assert.equal(calls.destroyed, 0);
    assert.equal(manager.getCurrentPlayer(), player);
    emit('timeupdate', {});
    assert.equal(calls.reports.at(-1)[0], 'progress');
    emit('error', {}, { type: 'failure' });
    assert.equal(calls.reports.at(-1)[0], 'stop');
    assert.equal(calls.destroyed, 1);
});

test('late rejected recovery completion cannot disturb newer ordinary player', async () => {
    const { manager, stages, calls, player } = fixture();
    manager.getPlayerState = () => ({ PlayState: {}, NowPlayingItem: { Id: item.Id, ServerId: item.ServerId } });
    const pending = manager.recoveryV1.begin(request());
    stages.item.resolve(item); await tick();
    stages.profile.resolve({}); await tick();
    stages.playback.resolve({ data: { MediaSources: [structuredClone(source)] } });
    assert.equal((await pending).status, 'accepted');
    const ordinary = manager.play({ serverId: 'server-1', ids: ['item-1'], fullscreen: false });
    await tick(); await tick();
    stages.ordinary.resolve();
    await ordinary; await tick();
    const eventCount = calls.events.length;
    const reportCount = calls.reports.length;
    stages.player.reject(new Error('late failure'));
    await tick();
    assert.equal(calls.events.length, eventCount);
    assert.equal(calls.reports.length, reportCount);
    assert.equal(calls.destroyed, 0);
    assert.equal(manager.getCurrentPlayer(), player);
});

test('reentrant Stop during current-player selection prevents later mutation', async () => {
    const { manager, stages, calls, player } = fixture();
    const pending = manager.recoveryV1.begin(request());
    stages.item.resolve(item); await tick();
    stages.profile.resolve({}); await tick();
    stages.playback.resolve({ data: { MediaSources: [structuredClone(source)] } });
    assert.equal((await pending).status, 'accepted');
    player.canPlayMediaType = () => { manager.stop(); return true; };
    stages.player.resolve(); await tick();
    assert.equal(manager.getCurrentPlayer(), undefined);
    assert.equal(calls.events.length, 0);
    assert.equal(calls.reports.length, 0);
});

test('late start-report completion cannot emit event after Stop', async () => {
    const { manager, stages, calls, client } = fixture();
    manager.getPlayerState = () => ({ PlayState: {}, NowPlayingItem: { Id: item.Id, ServerId: item.ServerId } });
    const report = deferred();
    client.reportPlaybackStart = info => { calls.reports.push(['start', info]); return report.promise; };
    const pending = manager.recoveryV1.begin(request());
    stages.item.resolve(item); await tick();
    stages.profile.resolve({}); await tick();
    stages.playback.resolve({ data: { MediaSources: [structuredClone(source)] } });
    assert.equal((await pending).status, 'accepted');
    stages.player.resolve(); await tick();
    assert.equal(calls.reports.length, 1);
    await manager.stop();
    const events = calls.events.length;
    report.resolve(); await tick();
    assert.equal(calls.events.length, events);
});

async function ordinaryOutcome(baseline) {
    const { manager, stages, calls } = fixture(null, null, baseline);
    manager.getPlayerState = () => ({ PlayState: {}, NowPlayingItem: { Id: item.Id, ServerId: item.ServerId } });
    const pending = manager.play({ serverId: 'server-1', ids: ['item-1'], fullscreen: false });
    stages.item.resolve(item); await tick();
    stages.profile.resolve({}); await tick();
    stages.playback.resolve({ data: { MediaSources: [structuredClone(source)] } });
    await tick();
    stages.player.resolve();
    await pending; await tick();
    return { playMethod: calls.play[0].playMethod, url: calls.play[0].url,
        reports: calls.reports.map(entry => entry[0]), eventNames: calls.events.map(entry => entry[1]) };
}

test('ordinary playback without capability matches pristine manager behavior', async () => {
    assert.deepEqual(await ordinaryOutcome(false), await ordinaryOutcome(true));
});
