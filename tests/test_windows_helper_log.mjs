import { strict as assert } from 'node:assert';
import { test } from 'node:test';
import { cleanHelperLog } from './windows_helper_log.mjs';

const clean = ['app wait 0 exit 0', 'waitResult 0 waitError 0 exitKnown true exitError 0',
  'activeBeforeKnown true activeBefore 0 activeBeforeError 0',
  'terminationRequested false terminationSucceeded false terminationError 0',
  'activeAfterKnown true activeAfter 0 activeAfterError 0 overall pass',
  'forcedCleanup false; job active processes 0', ''].join('\r\n');

test('only a known natural zero exit with a known empty job passes', () => {
  assert.equal(cleanHelperLog(clean, 0), true);
  for (const log of [clean.replace('exit 0\r\n', 'exit 42\r\n'),
    clean.replace('exitKnown true', 'exitKnown false'),
    clean.replace('activeBeforeKnown true', 'activeBeforeKnown false'),
    clean.replace('activeAfterKnown true', 'activeAfterKnown false'),
    clean + 'app wait 0 exit 42\r\n']) assert.equal(cleanHelperLog(log, 0), false);
  assert.equal(cleanHelperLog(clean, 7), false);
});
