import { strict as assert } from 'node:assert';
import { test } from 'node:test';
import { caseResult, finalizeCase } from './windows_smoke_result.mjs';

const clean = ['app pid 42', 'app wait 0 exit 0', 'waitResult 0 waitError 0 exitKnown true exitError 0',
  'activeBeforeKnown true activeBefore 0 activeBeforeError 0',
  'terminationRequested false terminationSucceeded false terminationError 0',
  'activeAfterKnown true activeAfter 0 activeAfterError 0 overall pass',
  'forcedCleanup false; job active processes 0', ''].join('\r\n');
const spec = { name: 'normal' };
const observation = { page: { title: 'Jellyfin', text: 'Server Address\nCONNECT' },
  gpu: { featureStatus: { gpu_compositing: 'enabled' }, commandLine: '' }, exitRequested: true };

function result(log, exit = 0) {
  const outcome = { log, observation, closed: { code: exit, signal: null } };
  return finalizeCase(spec, caseResult(spec, '/owned/cache', outcome), outcome.log);
}

test('the constructed result validates the full clean lifecycle log', () => {
  const value = result(clean);
  assert.deepEqual(value.failures, []);
  assert.match(value.log, /app wait 0 exit 0/);
  assert.doesNotMatch(value.log, /waitResult/);
});

test('natural nonzero exit remains a failure', () => {
  assert.ok(result(clean.replace('app wait 0 exit 0', 'app wait 0 exit 42'), 42).failures.includes('unclean exit'));
});

test('unknown lifecycle outcomes remain failures', () => {
  for (const entry of ['exitKnown true', 'activeBeforeKnown true', 'activeAfterKnown true'])
    assert.ok(result(clean.replace(entry, entry.replace('true', 'false'))).failures.includes('unclean exit'));
});

test('contradictory and duplicate lifecycle records remain failures', () => {
  for (const extra of ['app wait 0 exit 42', 'waitResult 0 waitError 0 exitKnown true exitError 0',
    'terminationRequested true terminationSucceeded true terminationError 0'])
    assert.ok(result(clean + extra + '\r\n').failures.includes('unclean exit'));
});
