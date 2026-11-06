import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';

const source = await readFile(new URL('../patches/jellyfin-web/recoveryTransaction.js', import.meta.url), 'utf8');
const { RecoveryTransaction } = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
const request = (attemptId = 'attempt-1') => ({ hostSession: 'host-1', producerGeneration: 'document-1',
    attemptId, serverId: 'server-1', accountId: 'account-1', itemId: 'item-1', sourceId: 'source-1',
    positionTicks: 10000, state: 'paused', mediaType: 'Video', seekable: true, localPlayer: true,
    singleItem: true, live: false, remote: false, tracksVerified: true, timelineVerified: true,
    audioStreamIndex: 0, subtitleStreamIndex: -1 });
const key = value => ({ hostSession: value.hostSession, producerGeneration: value.producerGeneration,
    attemptId: value.attemptId });
const tick = () => new Promise(resolve => setImmediate(resolve));
const boundary = { kind: 'w763-test-only', isTrusted: () => true, verifyEvidence: () => true };

test('invalidation before dispatch settles once and revokes ownership', async () => {
    const transactions = new RecoveryTransaction(boundary);
    let record;
    const result = transactions.begin(request(), item => { record = item; });
    await tick();
    transactions.invalidate();
    assert.equal((await result).status, 'canceled');
    assert.equal(transactions.accept(record), false);
    assert.equal(transactions.reject(record), false);
});

test('accepted attempt remains unsettled across invalidation until matching evidence', async () => {
    const transactions = new RecoveryTransaction(boundary);
    const first = request();
    const result = transactions.begin(first, record => { transactions.dispatching(record); transactions.accept(record); });
    assert.equal((await result).status, 'accepted');
    transactions.invalidate();
    assert.equal((await transactions.begin(request('attempt-2'), () => {})).status, 'rejected');
    assert.equal(transactions.observe({ ...key(first), kind: 'terminal' }), true);
    assert.equal(transactions.observe({ ...key(first), hostSession: 'other', kind: 'settled' }), false);
    assert.equal(transactions.observe({ ...key(first), kind: 'terminal' }), false);
    assert.equal(transactions.observe({ ...key(first), kind: 'settled' }), true);
    assert.equal((await transactions.begin(request('attempt-3'), record => transactions.reject(record))).status, 'rejected');
});

test('malformed request and replay fail without echoing unsafe identifiers', async () => {
    const transactions = new RecoveryTransaction(boundary);
    const unsafe = { ...request(), attemptId: 'https://secret', url: 'secret' };
    const result = await transactions.begin(unsafe, () => assert.fail('must not dispatch'));
    assert.deepEqual(result, { hostSession: 'host-1', producerGeneration: 'document-1', attemptId: null,
        status: 'rejected' });
    const valid = request();
    await transactions.begin(valid, record => transactions.reject(record));
    assert.equal((await transactions.begin(valid, () => assert.fail('must not replay'))).status, 'rejected');
});
