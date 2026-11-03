// Diagnostic: runs the real application on a non-input desktop for 25 seconds.
import { spawn } from 'node:child_process';
import { mkdir, mkdtemp, open, rm, writeFile, readFile, lstat } from 'node:fs/promises';
import { randomUUID } from 'node:crypto';
import net from 'node:net';
import path from 'node:path';
import { once } from 'node:events';

const [helper, build, qt, mode] = process.argv.slice(2);
if (![helper, build, qt].every(value => value && path.isAbsolute(value))) {
  throw new Error('Pass absolute paths to helper, integration build, and Qt root');
}
if (mode && !['--baseline', '--log-failure', '--window-actions', '--geometry', '--packaged'].includes(mode)) throw new Error('Unknown mode');
const marker = path.join(build, 'src', 'portable');
const outputRoot = path.join(build, 'desktop-smoke-native');
let output;
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
async function bounded(promise, ms, fallback) {
  let timer;
  try { return await Promise.race([promise, new Promise(resolve => { timer = setTimeout(() => resolve(fallback), ms); })]); }
  finally { clearTimeout(timer); }
}

async function portNumber() {
  const server = net.createServer();
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const port = server.address().port;
  await new Promise(resolve => server.close(resolve));
  return port;
}

async function getJson(url) {
  const response = await fetch(url, { signal: AbortSignal.timeout(1500) });
  if (!response.ok) throw new Error(`HTTP ${response.status}`);
  return response.json();
}

function caseEnvironment(spec, dir, port) {
  const env = { ...process.env };
  if (mode === '--packaged') {
    for (const key of Object.keys(env)) {
      if (/^(QT|QML|CMAKE_PREFIX_PATH|VCPKG_ROOT|OPENSSL_CONF|OPENSSL_MODULES|SSL_CERT_FILE|SSL_CERT_DIR)/i.test(key) ||
          key.toUpperCase() === 'PATH') delete env[key];
    }
    const windows = process.env.SystemRoot || process.env.WINDIR;
    if (!windows || !path.isAbsolute(windows)) throw new Error('Windows system directory is unavailable');
    env.PATH = [path.join(windows, 'System32'), windows, path.join(windows, 'System32', 'Wbem')].join(';');
  } else {
    env.PATH = `${path.join(qt, 'bin')};${process.env.PATH}`;
    env.QT_PLUGIN_PATH = path.join(qt, 'plugins');
    env.QML2_IMPORT_PATH = path.join(qt, 'qml');
  }
  return { ...env, QT_QPA_PLATFORM: 'windows', QTWEBENGINE_REMOTE_DEBUGGING: `127.0.0.1:${port}`,
    QTWEBENGINE_CHROMIUM_FLAGS: spec.flags || '', QT_LOGGING_RULES: 'qt.webenginecontext.debug=true',
    LOCALAPPDATA: dir, APPDATA: dir, TEMP: path.join(dir, 'temp'), TMP: path.join(dir, 'temp') };
}

async function cdp(url, method, params = {}) {
  const socket = new WebSocket(url);
  try {
    await Promise.race([once(socket, 'open'), delay(4000).then(() => { throw new Error('CDP timeout'); })]);
    const result = new Promise((resolve, reject) => {
      socket.addEventListener('message', event => {
        const value = JSON.parse(event.data);
        if (value.id === 1) value.error ? reject(new Error(JSON.stringify(value.error))) : resolve(value.result);
      });
    });
    socket.send(JSON.stringify({ id: 1, method, params }));
    return await Promise.race([result, delay(4000).then(() => { throw new Error('CDP response timeout'); })]);
  } finally { socket.close(); }
}

async function observe(port, child, dir, requestExit = true) {
  const url = `http://127.0.0.1:${port}`;
  for (let i = 0; i < 35; i++) {
    if (child.diagnosticClosed || child.exitCode !== null || child.signalCode !== null)
      throw new Error(`helper exited ${child.exitCode ?? child.signalCode}`);
    try {
      const version = await getJson(`${url}/json/version`);
      const targets = await getJson(`${url}/json/list`);
      const target = targets.find(item => item.type === 'page');
      if (!target) throw new Error('No page target');
      const page = await cdp(target.webSocketDebuggerUrl, 'Runtime.evaluate',
        { expression: '({url:location.href,title:document.title,text:document.body?.innerText?.slice(0,1000),exitType:typeof window.api?.system?.exit})', returnByValue: true });
      const state = page.result.value;
      if (state.title !== 'Jellyfin' || !state.text?.includes('Server Address') ||
          !state.text.includes('CONNECT') || state.exitType !== 'function') throw new Error('Page still loading');
      const gpu = await cdp(version.webSocketDebuggerUrl, 'SystemInfo.getInfo');
      if (requestExit) {
        const screenshot = await cdp(target.webSocketDebuggerUrl, 'Page.captureScreenshot');
        await writeFile(path.join(dir, 'page.png'), Buffer.from(screenshot.data, 'base64'));
      }
      if (requestExit && page.result.value.exitType === 'function') {
        await cdp(target.webSocketDebuggerUrl, 'Runtime.evaluate',
          { expression: "setTimeout(() => window.api.system.exit(), 250); 'requested'", returnByValue: true });
      }
      return { page: page.result.value, gpu: { featureStatus: gpu.gpu.featureStatus,
        displayType: gpu.gpu.auxAttributes.displayType, renderer: gpu.gpu.auxAttributes.glRenderer,
        commandLine: gpu.commandLine }, exitRequested: page.result.value.exitType === 'function',
        raw: { version, targets, page, gpu } };
    } catch { await delay(500); }
  }
  throw new Error('WebEngine debug endpoint unavailable');
}

async function runProcess(spec, dir, data, port, env) {
  const child = spawn(helper, [path.join(build, 'src', 'Jellyfin Desktop.exe'), data, ...(spec.arg ? [spec.arg] : [])],
    { env, cwd: mode === '--packaged' ? path.join(build, 'src') : undefined,
      windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] });
  let log = '';
  let stdinError;
  child.stdin.on('error', error => { stdinError = String(error); });
  child.stdout.on('data', chunk => { log += chunk; });
  child.stderr.on('data', chunk => { log += chunk; });
  const completion = new Promise(resolve => {
    let spawnError;
    child.on('error', error => { spawnError = String(error); });
    child.on('close', (code, signal) => { child.diagnosticClosed = true; resolve({ code, signal, spawnError }); });
  });
  let observation;
  try {
    const work = async () => {
      if (spec.windowActions) {
        await observe(port, child, dir, false);
        child.stdin.end('x');
        for (let i = 0; i < 240 && !/window actions (passed|failed)/.test(log) &&
             child.exitCode === null && child.signalCode === null && !stdinError; i++) await delay(250);
        if (stdinError) throw new Error(`helper stdin: ${stdinError}`);
      }
      const result = await observe(port, child, dir);
      if (spec.windowActions && !log.includes('window actions passed')) result.actionError = 'Window actions failed or timed out';
      return result;
    };
    observation = await bounded(Promise.race([work(), completion.then(result => {
      throw new Error(`helper closed: ${JSON.stringify(result)}`);
    })]), 90000, { error: 'case deadline exceeded' });
  } catch (error) { observation = { error: String(error) }; }
  let closed = await bounded(completion, 15000, null);
  if (!closed) {
    child.kill();
    closed = await bounded(completion, 5000, null);
    observation.cleanupError = closed ? 'helper terminated after deadline' : 'helper did not close after termination';
  }
  return { observation, closed, log };
}

async function runCase(spec) {
    const dir = path.join(output, spec.name);
    const data = path.join(spec.sharedData ? path.join(output, 'geometry-profile') : dir, 'data');
    const profile = spec.profile;
    const cacheDir = path.join(build, 'src', 'cache', 'profiles', profile);
    await mkdir(dir, { recursive: true });
    let log = '';
    let result = { case: spec.name, cacheDir, observation: { error: 'case did not start' }, helperExit: null, log: '' };
    try {
    await mkdir(path.join(data, 'profiles', profile), { recursive: true });
    await mkdir(path.join(dir, 'temp'), { recursive: true });
    if (!spec.reuseData) {
      await writeFile(path.join(data, 'profiles.json'), JSON.stringify({ defaultProfile: profile }));
      await writeFile(path.join(data, 'profiles', profile, 'profile.json'), JSON.stringify({ name: 'Smoke' }));
      await writeFile(path.join(data, 'profiles', profile, 'jellyfin-desktop.conf'),
        JSON.stringify({ version: 7, sections: { main: { allowBrowserZoom: !spec.disablePinch } } }));
    }
    if (spec.logFailure) await writeFile(path.join(data, 'profiles', profile, 'logs'), 'block log directory');
    const port = await portNumber();
    const env = caseEnvironment(spec, dir, port);
    const outcome = await runProcess(spec, dir, data, port, env);
    log = outcome.log;
    const diagnostic = log.split(/\r?\n/).filter(line =>
      /app pid|app wait|job active|window actions|window bounds|Failed to create temporary log|ERROR:/.test(line)).join('\n');
    result = { case: spec.name, cacheDir, observation: outcome.observation,
      helperExit: outcome.closed?.code ?? null, helperSignal: outcome.closed?.signal ?? null,
      spawnError: outcome.closed?.spawnError, log: diagnostic };
    if (mode === '--packaged') result.runtimeEnvironment = { path: env.PATH,
      qtVariableNames: Object.keys(env).filter(key => /^(QT|QML)/i.test(key)).sort() };
    if (spec.sharedData) {
      const storage = await readFile(path.join(data, 'profiles', profile, 'storage.json'), 'utf8');
      await writeFile(path.join(dir, 'storage.json'), storage);
      result.bounds = /window bounds (-?\d+ -?\d+ -?\d+ -?\d+)/.exec(log)?.[1];
    }
    } catch (error) { result.observation = { error: String(error) }; }
    finally {
      result.failures = checkCase(spec, result);
      await writeFile(path.join(dir, 'console.log'), log);
      await writeFile(path.join(dir, 'result.json'), JSON.stringify(result, null, 2));
    }
    return result;
}

function checkCase(spec, result) {
  const failures = [];
  const { observation, log, helperExit } = result;
  if ((helperExit !== 0 && !(spec.windowActions && helperExit === 7)) || !log.includes('app wait 0 exit 0') ||
      !log.includes('forcedCleanup false; job active processes 0')) failures.push('unclean exit');
  if (observation.error) return [...failures, observation.error];
  if (observation.cleanupError || result.spawnError || result.helperSignal)
    failures.push(observation.cleanupError || result.spawnError || `helper signal ${result.helperSignal}`);
  if (observation.actionError) failures.push(observation.actionError);
  const { page, gpu, exitRequested } = observation;
  if (page.title !== 'Jellyfin' || !page.text.includes('Server Address') ||
      !page.text.includes('CONNECT') || !exitRequested) failures.push('initial page or native exit');
  const disabled = spec.arg === '--disable-gpu' || spec.flags === '--disable-gpu';
  if (mode !== '--baseline' && gpu.featureStatus.gpu_compositing !== (disabled ? 'disabled_software' : 'enabled'))
    failures.push('unexpected GPU compositing');
  if (mode !== '--baseline' && disabled && !gpu.commandLine.includes('--disable-gpu')) failures.push('missing disable-gpu');
  if (mode === '--baseline' && (gpu.featureStatus.gpu_compositing !== 'enabled' ||
      (disabled && gpu.commandLine.includes('--disable-gpu')))) failures.push('unexpected baseline backend');
  if (spec.disablePinch && !gpu.commandLine.includes('--disable-pinch')) failures.push('missing disable-pinch');
  if (spec.name === 'quoted-flags' && !gpu.commandLine.includes('--user-agent="Desktop Smoke Test"'))
    failures.push('quoted argument not preserved');
  if (spec.logFailure && !log.includes('Failed to create temporary log file')) failures.push('missing log failure diagnostic');
  return failures;
}

async function main() {
  let ownsMarker = false;
  try {
    const markerFile = await open(marker, 'wx');
    ownsMarker = true;
    await markerFile.close();
  } catch (error) {
    if (ownsMarker) { await rm(marker); throw error; }
    if (mode !== '--packaged' || error.code !== 'EEXIST') throw error;
    const existing = await lstat(marker);
    if (!existing.isFile() || existing.size !== 0) throw new Error('Existing portable marker is not an empty file');
  }
  try {
    await mkdir(outputRoot, { recursive: true });
    output = await mkdtemp(path.join(outputRoot, 'run-'));
    console.log(`Evidence: ${output}`);
    const cases = [{ name: 'normal' }, { name: 'cli-disable-gpu', arg: '--disable-gpu' },
      { name: 'env-disable-gpu-pinch', flags: '--disable-gpu', disablePinch: true },
      { name: 'quoted-flags', flags: '--user-agent="Desktop Smoke Test"' }];
    const selected = mode === '--packaged' ? cases.slice(0, 1) : mode === '--baseline' ? cases.slice(0, 2) :
      mode === '--log-failure' ? [{ name: 'log-failure', logFailure: true }] :
      mode === '--geometry' ? [
        { name: 'geometry-1', arg: '--window-actions', windowActions: true, sharedData: true },
        { name: 'geometry-2', arg: '--geometry-observe', windowActions: true, sharedData: true, reuseData: true },
        { name: 'geometry-3', arg: '--geometry-observe', windowActions: true, sharedData: true, reuseData: true }
      ] :
      mode === '--window-actions' ? [{ name: 'window-actions', arg: '--window-actions', windowActions: true }] : cases;
    let firstBounds;
    const geometryProfile = randomUUID().replaceAll('-', '');
    for (const spec of selected) {
      spec.profile = spec.sharedData ? geometryProfile : randomUUID().replaceAll('-', '');
      const result = await runCase(spec);
      if (spec.sharedData) {
        if (!firstBounds) firstBounds = result.bounds;
        if (!result.bounds || result.bounds !== firstBounds) result.failures.push('restored bounds differ');
        await writeFile(path.join(output, spec.name, 'result.json'), JSON.stringify(result, null, 2));
      }
      delete result.observation.raw;
      console.log(JSON.stringify(result));
      if (result.failures.length) process.exitCode = 1;
    }
  } finally {
    if (ownsMarker) await rm(marker);
  }
}

main().catch(error => { console.error(error); process.exitCode = 1; });
