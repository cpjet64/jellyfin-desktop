/* Load the verified upstream manager with only its imports replaced by fakes. */
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { spawnSync } = require('node:child_process');

const root = path.resolve(__dirname, '..');
const sha = data => crypto.createHash('sha256').update(data).digest('hex');

function patchedSource(baseline = false) {
    const source = process.env.JELLYFIN_WEB_SOURCE;
    if (!source) throw new Error('JELLYFIN_WEB_SOURCE must name the verified pristine playbackmanager.js');
    const bytes = fs.readFileSync(source);
    assert.equal(bytes.length, 158528);
    assert.equal(sha(bytes), '43a9837e11041ddd624a6f68ee3fe3ffb7ee48dfa1443b27592a67c0c0895af2');
    if (baseline) return bytes.toString('utf8');
    const scratchRoot = path.join(root, 'deps');
    fs.mkdirSync(scratchRoot, { recursive: true });
    const scratch = fs.mkdtempSync(path.join(scratchRoot, 'w763-manager-'));
    try {
        const dest = path.join(scratch, 'src/components/playback/playbackmanager.js');
        fs.mkdirSync(path.dirname(dest), { recursive: true });
        fs.writeFileSync(dest, bytes, { flag: 'wx' });
        const patch = path.join(root, 'patches/jellyfin-web/0001-recovery-transaction.patch');
        const patchBytes = Buffer.from(fs.readFileSync(patch, 'utf8').replace(/\r\n/g, '\n'));
        assert.equal(sha(patchBytes), 'bd7091aabd6e471eed7838c9040b1a9e4ac3af7f562e3f28f17aa0b48b1ca88d');
        const applied = spawnSync('git', ['apply', '--no-index', '-'],
            { cwd: scratch, encoding: 'utf8', timeout: 10000, windowsHide: true,
                input: patchBytes, env: { ...process.env, GIT_CEILING_DIRECTORIES: path.dirname(scratch) } });
        assert.equal(applied.status, 0, applied.stderr);
        const result = fs.readFileSync(dest, 'utf8').replace(/\r\n/g, '\n');
        assert.equal(sha(Buffer.from(result)), '7954b11d008cb58684f9ab8fd1bb8569f6691fad69e9ed0a44ab09ee413582e8');
        assert.match(result, /this\.recoveryV1 = Object\.freeze/);
        return result;
    } finally { fs.rmSync(scratch, { recursive: true, force: true }); }
}

function loadManager(fakes, baseline = false) {
    const helperBytes = fs.readFileSync(path.join(root, 'patches/jellyfin-web/recoveryTransaction.js'), 'utf8')
        .replace(/\r\n/g, '\n');
    assert.equal(sha(Buffer.from(helperBytes)), '37e122c872fc4f52c75ab40c6ee675bc9a32df0480a8eafbdd7701e77b4d3a99');
    const helper = helperBytes.replace(/^export /gm, '');
    const manager = patchedSource(baseline).replace(/^import .*;\s*$/gm, '')
        .replace(/^export class PlaybackManager/m, 'class PlaybackManager')
        .replace(/^export const playbackManager/m, 'const playbackManager');
    const globals = { console, Promise, setTimeout, clearTimeout, setInterval: () => 1, clearInterval() {}, URL, Response,
        window: { addEventListener() {} }, document: { addEventListener() {} },
        fetch: () => { throw new Error('network forbidden'); }, ...fakes };
    vm.createContext(globals);
    vm.runInContext(`${helper}\n${manager}\nglobalThis.Manager = PlaybackManager;`, globals,
        { filename: 'verified-patched-playbackmanager.js', timeout: 10000 });
    return new globals.Manager(fakes.__recoveryTestBoundary || null);
}

module.exports = { loadManager };
