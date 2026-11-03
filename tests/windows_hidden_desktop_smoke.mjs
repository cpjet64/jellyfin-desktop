// Diagnostic: runs the real application on a non-input desktop for 25 seconds.
import { spawn } from 'node:child_process';
import { mkdir, mkdtemp, open, rm, writeFile } from 'node:fs/promises';
import net from 'node:net';
import path from 'node:path';
import { once } from 'node:events';

const [helper, build, qt, mode] = process.argv.slice(2);
if (![helper, build, qt].every(value => value && path.isAbsolute(value))) {
  throw new Error('Pass absolute paths to helper, integration build, and Qt root');
}
if (mode && !['--baseline', '--log-failure', '--window-actions'].includes(mode)) throw new Error('Unknown mode');
const marker = path.join(build, 'src', 'portable');
const outputRoot = path.join(build, 'desktop-smoke-native');
let output;
const profile = '0123456789abcdef0123456789abcdef';
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));

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

async function observe(port, child, requestExit = true) {
  const url = `http://127.0.0.1:${port}`;
  for (let i = 0; i < 35; i++) {
    if (child.exitCode !== null) throw new Error(`helper exited ${child.exitCode}`);
    try {
      const version = await getJson(`${url}/json/version`);
      const targets = await getJson(`${url}/json/list`);
      const target = targets.find(item => item.type === 'page');
      if (!target) throw new Error('No page target');
      const page = await cdp(target.webSocketDebuggerUrl, 'Runtime.evaluate',
        { expression: '({url:location.href,title:document.title,text:document.body?.innerText?.slice(0,1000),exitType:typeof window.api?.system?.exit})', returnByValue: true });
      if (page.result.value.url === 'about:blank') throw new Error('Page still loading');
      const gpu = await cdp(version.webSocketDebuggerUrl, 'SystemInfo.getInfo');
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

async function runCase(spec) {
    const dir = path.join(output, spec.name);
    const data = path.join(dir, 'data');
    await mkdir(path.join(data, 'profiles', profile), { recursive: true });
    await mkdir(path.join(dir, 'temp'), { recursive: true });
    await writeFile(path.join(data, 'profiles.json'), JSON.stringify({ defaultProfile: profile }));
    await writeFile(path.join(data, 'profiles', profile, 'profile.json'), JSON.stringify({ name: 'Smoke' }));
    if (spec.logFailure) await writeFile(path.join(data, 'profiles', profile, 'logs'), 'block log directory');
    await writeFile(path.join(data, 'profiles', profile, 'jellyfin-desktop.conf'),
      JSON.stringify({ version: 7, sections: { main: { allowBrowserZoom: !spec.disablePinch } } }));
    const port = await portNumber();
    const env = { ...process.env, PATH: `${path.join(qt, 'bin')};${process.env.PATH}`,
      QT_PLUGIN_PATH: path.join(qt, 'plugins'), QML2_IMPORT_PATH: path.join(qt, 'qml'),
      QT_QPA_PLATFORM: 'windows', QTWEBENGINE_REMOTE_DEBUGGING: `127.0.0.1:${port}`,
      QTWEBENGINE_CHROMIUM_FLAGS: spec.flags || '', QT_LOGGING_RULES: 'qt.webenginecontext.debug=true', LOCALAPPDATA: dir, APPDATA: dir,
      TEMP: path.join(dir, 'temp'), TMP: path.join(dir, 'temp') };
    const child = spawn(helper, [path.join(build, 'src', 'Jellyfin Desktop.exe'), data, ...(spec.arg ? [spec.arg] : [])],
      { env, windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] });
    let log = '';
    child.stdout.on('data', chunk => { log += chunk; });
    child.stderr.on('data', chunk => { log += chunk; });
    let observation;
    try {
      if (spec.windowActions) {
        await observe(port, child, false);
        child.stdin.end('x');
        for (let i = 0; i < 240 && !/window actions (passed|failed)/.test(log) && child.exitCode === null; i++) await delay(250);
      }
      observation = await observe(port, child);
      if (spec.windowActions && !log.includes('window actions passed'))
        observation.actionError = 'Window actions failed or timed out';
    }
    catch (error) { observation = { error: String(error) }; }
    const [code] = child.exitCode === null ? await once(child, 'exit') : [child.exitCode];
    await writeFile(path.join(dir, 'console.log'), log);
    const diagnostic = log.split(/\r?\n/).filter(line =>
      /app pid|app wait|job active|window actions|Failed to create temporary log|ERROR:/.test(line)).join('\n');
    const result = { case: spec.name, observation, helperExit: code, log: diagnostic };
    result.failures = checkCase(spec, result);
    await writeFile(path.join(dir, 'result.json'), JSON.stringify(result, null, 2));
    delete result.observation.raw;
    return result;
}

function checkCase(spec, result) {
  const failures = [];
  const { observation, log, helperExit } = result;
  if ((helperExit !== 0 && !(spec.windowActions && helperExit === 7)) || !log.includes('app wait 0 exit 0') ||
      !log.includes('forcedCleanup false; job active processes 0')) failures.push('unclean exit');
  if (observation.error) return [...failures, observation.error];
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
  const markerFile = await open(marker, 'wx');
  await markerFile.close();
  try {
    await mkdir(outputRoot, { recursive: true });
    output = await mkdtemp(path.join(outputRoot, 'run-'));
    console.log(`Evidence: ${output}`);
    const cases = [{ name: 'normal' }, { name: 'cli-disable-gpu', arg: '--disable-gpu' },
      { name: 'env-disable-gpu-pinch', flags: '--disable-gpu', disablePinch: true },
      { name: 'quoted-flags', flags: '--user-agent="Desktop Smoke Test"' }];
    const selected = mode === '--baseline' ? cases.slice(0, 2) :
      mode === '--log-failure' ? [{ name: 'log-failure', logFailure: true }] :
      mode === '--window-actions' ? [{ name: 'window-actions', arg: '--window-actions', windowActions: true }] : cases;
    for (const spec of selected) {
      const result = await runCase(spec);
      console.log(JSON.stringify(result));
      if (result.failures.length) process.exitCode = 1;
    }
  } finally {
    await rm(marker);
  }
}

main().catch(error => { console.error(error); process.exitCode = 1; });
