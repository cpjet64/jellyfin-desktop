import { spawn } from 'node:child_process';
// Diagnostic only: the offscreen platform cannot prove GPU rendering, clean exit,
// or child cleanup after a parent crash. This script never returns an acceptance pass.
import { mkdir, open, rm, writeFile, access, realpath } from 'node:fs/promises';
import net from 'node:net';
import path from 'node:path';
import { once } from 'node:events';

const [buildArg, qtArg, ...extraArgs] = process.argv.slice(2);
if (!buildArg || !qtArg || extraArgs.length || !path.isAbsolute(buildArg) || !path.isAbsolute(qtArg)) {
  console.error('Usage: node windows_desktop_smoke.mjs <absolute-build-dir> <absolute-qt-dir>');
  process.exit(2);
}
const build = await realpath(buildArg);
const qt = await realpath(qtArg);
const executable = path.join(build, 'src', 'Jellyfin Desktop.exe');
const marker = path.join(build, 'src', 'portable');
const cache = path.join(build, 'src', 'cache');
const output = path.join(build, 'desktop-smoke');
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));

async function unusedPort() {
  const server = net.createServer();
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const port = server.address().port;
  await new Promise(resolve => server.close(resolve));
  return port;
}

async function getJson(url, options = {}) {
  const response = await fetch(url, { ...options, signal: AbortSignal.timeout(2000) });
  if (!response.ok) throw new Error(`HTTP ${response.status}: ${url}`);
  return response.json();
}

async function waitForDebug(port, child) {
  const deadline = Date.now() + 30000;
  while (Date.now() < deadline) {
    if (child.exitCode !== null) throw new Error(`app exited: ${child.exitCode}`);
    try { return await getJson(`http://127.0.0.1:${port}/json/version`); }
    catch { await delay(500); }
  }
  throw new Error('debug endpoint did not start within 30 seconds');
}

async function cdp(url, method, params = {}) {
  const socket = new WebSocket(url);
  try {
    await Promise.race([once(socket, 'open'), delay(5000).then(() => { throw new Error('CDP connect timeout'); })]);
    const response = new Promise((resolve, reject) => {
      socket.addEventListener('message', event => {
        const data = JSON.parse(event.data);
        if (data.id === 1) data.error ? reject(new Error(JSON.stringify(data.error))) : resolve(data.result);
      });
    });
    socket.send(JSON.stringify({ id: 1, method, params }));
    return await Promise.race([response, delay(5000).then(() => { throw new Error('CDP response timeout'); })]);
  } finally { socket.close(); }
}

async function stopTree(child) {
  if (child.exitCode !== null) return;
  const killer = spawn('taskkill.exe', ['/PID', String(child.pid), '/T', '/F'],
    { windowsHide: true, stdio: 'ignore' });
  await Promise.race([once(killer, 'exit'), delay(5000)]);
  await Promise.race([once(child, 'exit'), delay(5000)]);
  if (child.exitCode === null) throw new Error(`app process ${child.pid} did not exit`);
}

async function runCase(spec) {
  const dir = path.join(output, `${spec.name}-${Date.now()}`);
  const profile = '0123456789abcdef0123456789abcdef';
  const data = path.join(dir, 'data');
  const profileDir = path.join(data, 'profiles', profile);
  await mkdir(profileDir, { recursive: true });
  await mkdir(path.join(dir, 'temp'), { recursive: true });
  await writeFile(path.join(data, 'profiles.json'), JSON.stringify({ defaultProfile: profile }));
  await writeFile(path.join(profileDir, 'profile.json'), JSON.stringify({ name: 'Smoke' }));
  await writeFile(path.join(profileDir, 'jellyfin-desktop.conf'),
    JSON.stringify({ version: 7, sections: { main: { allowBrowserZoom: !spec.disablePinch } } }));
  const port = await unusedPort();
  const env = { ...process.env, PATH: `${path.join(qt, 'bin')};${process.env.PATH}`,
    QT_PLUGIN_PATH: path.join(qt, 'plugins'), QML2_IMPORT_PATH: path.join(qt, 'qml'),
    QT_QPA_PLATFORM: 'offscreen', QTWEBENGINE_REMOTE_DEBUGGING: `127.0.0.1:${port}`,
    QT_LOGGING_RULES: 'qt.webenginecontext.debug=true', QTWEBENGINE_CHROMIUM_FLAGS: spec.flags || '',
    LOCALAPPDATA: dir, APPDATA: dir, TEMP: path.join(dir, 'temp'), TMP: path.join(dir, 'temp') };
  const child = spawn(executable, ['--config-dir', data, ...spec.args], { env, windowsHide: true });
  let log = '';
  child.stdout.on('data', chunk => { log += chunk; });
  child.stderr.on('data', chunk => { log += chunk; });
  const result = { case: spec.name, dir, cleanExit: false,
    cleanupLimitation: 'Descendants cannot be verified if the parent exits before taskkill.' };
  try {
    const version = await waitForDebug(port, child);
    const base = `http://127.0.0.1:${port}`;
    result.gpu = await cdp(version.webSocketDebuggerUrl, 'SystemInfo.getInfo');
    result.targets = await getJson(`${base}/json/list`);
    const target = result.targets.find(item => item.type === 'page');
    if (!target) throw new Error('No WebEngine page target found');
    result.page = await cdp(target.webSocketDebuggerUrl, 'Runtime.evaluate',
      { expression: '({url: location.href, title: document.title, text: document.body?.innerText?.slice(0, 1000), userAgent: navigator.userAgent})', returnByValue: true });
    result.status = 'observed';
  } catch (error) { result.status = 'failed'; result.error = String(error); }
  finally {
    await stopTree(child);
    await writeFile(path.join(dir, 'console.log'), log);
    await writeFile(path.join(dir, 'result.json'), JSON.stringify(result, null, 2));
  }
  return result;
}

async function main() {
  try { await access(cache); throw new Error(`Refusing pre-existing portable cache: ${cache}`); }
  catch (error) { if (error.code !== 'ENOENT') throw error; }
  const markerFile = await open(marker, 'wx');
  await markerFile.close();
  try {
    const cases = [
      { name: 'normal', args: [] },
      { name: 'cli-disable-gpu', args: ['--disable-gpu'] },
      { name: 'env-disable-gpu-pinch', args: [], flags: '--disable-gpu', disablePinch: true },
      { name: 'quoted-flags', args: [], flags: '--user-agent="Desktop Smoke Test"' }
    ];
    const results = [];
    for (const spec of cases) results.push(await runCase(spec));
    console.log(JSON.stringify(results, null, 2));
    console.error('Diagnostic only: observations do not constitute an acceptance pass.');
    process.exitCode = 2;
  } finally { await rm(marker); }
}

main().catch(error => { console.error(error); process.exitCode = 1; });
