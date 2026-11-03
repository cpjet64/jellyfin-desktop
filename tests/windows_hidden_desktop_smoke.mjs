// Diagnostic: runs the real application on a non-input desktop for 25 seconds.
import { spawn } from 'node:child_process';
import { mkdir, open, rm, writeFile } from 'node:fs/promises';
import net from 'node:net';
import path from 'node:path';
import { once } from 'node:events';

const [helper, build, qt, mode] = process.argv.slice(2);
if (![helper, build, qt].every(value => value && path.isAbsolute(value))) {
  throw new Error('Pass absolute paths to helper, integration build, and Qt root');
}
if (mode && !['--baseline', '--log-failure'].includes(mode)) throw new Error('Unknown mode');
const marker = path.join(build, 'src', 'portable');
const output = path.join(build, 'desktop-smoke-native');
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

async function observe(port, child) {
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
      if (page.result.value.exitType === 'function') {
        await cdp(target.webSocketDebuggerUrl, 'Runtime.evaluate', { expression: 'window.api.system.exit()' }).catch(() => {});
      }
      return { page: page.result.value, gpu: { featureStatus: gpu.gpu.featureStatus,
        displayType: gpu.gpu.auxAttributes.displayType, renderer: gpu.gpu.auxAttributes.glRenderer,
        commandLine: gpu.commandLine } };
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
      { env, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
    let log = '';
    child.stdout.on('data', chunk => { log += chunk; });
    child.stderr.on('data', chunk => { log += chunk; });
    let observation;
    try { observation = await observe(port, child); }
    catch (error) { observation = { error: String(error) }; }
    const [code] = child.exitCode === null ? await once(child, 'exit') : [child.exitCode];
    const diagnostic = log.split(/\r?\n/).filter(line =>
      /app pid|app wait|job active|Failed to create temporary log|ERROR:/.test(line)).join('\n');
    return { case: spec.name, observation, helperExit: code, log: diagnostic };
}

async function main() {
  const markerFile = await open(marker, 'wx');
  await markerFile.close();
  try {
    await mkdir(output);
    const cases = [{ name: 'normal' }, { name: 'cli-disable-gpu', arg: '--disable-gpu' },
      { name: 'env-disable-gpu-pinch', flags: '--disable-gpu', disablePinch: true },
      { name: 'quoted-flags', flags: '--user-agent="Desktop Smoke Test"' }];
    const selected = mode === '--baseline' ? cases.slice(0, 2) :
      mode === '--log-failure' ? [{ name: 'log-failure', logFailure: true }] : cases;
    for (const spec of selected) {
      const result = await runCase(spec);
      console.log(JSON.stringify(result));
      if (result.helperExit !== 0 || result.observation.error) process.exitCode = 1;
    }
  } finally {
    await rm(marker);
  }
}

main().catch(error => { console.error(error); process.exitCode = 1; });
