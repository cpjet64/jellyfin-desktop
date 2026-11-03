// Exercises harness process failures without launching the application.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { mkdir, mkdtemp, readFile, readdir, rm, stat, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const scratch = path.join(root, 'build');
const script = path.join(root, 'tests', 'windows_hidden_desktop_smoke.mjs');

async function run(helper, build, mode) {
  const child = spawn(process.execPath, [script, helper, build, build, mode], { windowsHide: true });
  let stdout = '';
  let stderr = '';
  child.stdout.on('data', data => { stdout += data; });
  child.stderr.on('data', data => { stderr += data; });
  const exit = await new Promise((resolve, reject) => {
    child.on('error', reject);
    child.on('close', (code, signal) => resolve({ code, signal }));
  });
  assert.equal(exit.code, 1, stderr);
  assert.equal(exit.signal, null);
  const match = /^Evidence: (.+)$/m.exec(stdout);
  assert.ok(match, stdout + stderr);
  const evidence = match[1].trim();
  const cases = (await readdir(evidence)).filter(name => name !== 'geometry-profile');
  return Promise.all(cases.map(async name => JSON.parse(await readFile(path.join(evidence, name, 'result.json')))));
}

async function main() {
  await mkdir(scratch, { recursive: true });
  const dir = await mkdtemp(path.join(scratch, 'hidden-fixture-'));
  try {
    await mkdir(path.join(dir, 'src'));
    const missing = await run(path.join(dir, 'missing.exe'), dir, '--baseline');
    assert.equal(missing.length, 2);
    assert.ok(missing.every(result => result.failures.length && result.spawnError));
    assert.notEqual(missing[0].cacheDir, missing[1].cacheDir);
    const geometry = await run(path.join(dir, 'missing.exe'), dir, '--geometry');
    assert.equal(geometry.length, 3);
    assert.equal(new Set(geometry.map(result => result.cacheDir)).size, 1);
    assert.notEqual(geometry[0].cacheDir, missing[0].cacheDir);
    const marker = path.join(dir, 'src', 'portable');
    await writeFile(marker, '');
    const early = await run(process.execPath, dir, '--packaged');
    assert.equal(early.length, 1);
    assert.ok(early[0].failures.length);
    assert.equal((await stat(marker)).size, 0);
    console.log('Missing helper, early exit, unique caches, and packaged marker preservation passed.');
  } finally { await rm(dir, { recursive: true, force: true }); }
}

main().catch(error => { console.error(error); process.exitCode = 1; });
