import hashlib
import json
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path('E:/CursorAI/JellyfinDesktop')
ARCHIVE = ROOT / 'deps/reconcile-20261113/retained-superseded'
MANIFEST_SHA = 'b2275773776cf5098b912380b5048c633844bee199f07c97b7b598c105958975'
BUNDLE_SHA = '621b082c685906cd0a02b450b4fb520fd6b842402a4662c3d405f928112cc87b'
SUB_BUNDLE_SHA = '88c33c37ac2dac70f9f185d733001c59ca7442513e1a6600544be5e22183c5f8'
TARGET = '6b32a49d9dd0efb1ed18bf0c98698af83c8ac4e0'
SOURCE = 'b3986ab35cca5bbd0e3a723b353daef20059e47d'
NAMES = ('c2a-fixtures', 'w1021-continuation', 'w1021-reservation', 'w1021-wiring',
         'w796-restart-broker', 'w904-closure', 'w929-identity-commitment',
         'w931-broker-closed', 'w939-windows-tests', 'w965-final', 'w965-session-wiring',
         'w981-adapter-tests', 'w997-coherent', 'w425-mac-helpers', 'w915-closure')
ENV = dict(os.environ, GIT_OPTIONAL_LOCKS='0')
FLAGS = getattr(subprocess, 'CREATE_NO_WINDOW', 0)


def git(path, *args):
    return subprocess.run(['git', '-C', str(path), *args], stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, env=ENV, creationflags=FLAGS,
                          check=True).stdout


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def plain(path, boundary):
    current = path
    while current != boundary.parent:
        stat = current.lstat()
        require(not current.is_symlink() and not stat.st_file_attributes & 0x400,
                f'reparse point: {current}')
        current = current.parent


def nested_git(root):
    found = []
    for parent, dirs, files in os.walk(root, followlinks=False):
        here = pathlib.Path(parent)
        for name in list(dirs):
            child = here / name
            plain(child, root)
            if name.lower() in ('.ssh', '.aws', '.gnupg', '.azure'):
                raise RuntimeError(f'secret client directory: {child}')
            if name == '.git':
                found.append(str(child.relative_to(root)))
                dirs.remove(name)
        if '.git' in files and here != root:
            found.append(str((here / '.git').relative_to(root)))
    return sorted(found)


def compare_files(root, name, item, kind):
    records = item[kind]
    for record in records:
        relative = pathlib.PurePosixPath(record['path'])
        require(not relative.is_absolute() and '..' not in relative.parts and
                '.git' not in relative.parts, f'unsafe path: {relative}')
        source = root.joinpath(*relative.parts)
        target = ARCHIVE / name / kind / pathlib.Path(*relative.parts)
        plain(source, root)
        plain(target, ARCHIVE)
        require(source.is_file() and target.is_file(), f'missing file: {source}')
        require(source.stat().st_size == target.stat().st_size == record['bytes'], f'size: {source}')
        expected = record['source_before']
        require(expected == record['source_after'] == record['destination'], f'bad manifest: {source}')
        require(digest(source) == expected and digest(target) == expected, f'hash: {source}')


def check_submodule(root, item, sub):
    path = root / 'external/mpvqt'
    actual = git(root, 'submodule', 'status', '--recursive').decode(errors='replace')
    require(actual == item['submodules'], f'submodule status: {root}')
    if root.name != 'c2a-fixtures':
        require(actual.startswith('-') and (not path.exists() or not any(path.iterdir())),
                f'initialized or nonempty submodule: {root}')
        return
    require(digest(ARCHIVE / 'c2a-fixtures/submodule-mpvqt/all-refs.bundle') == SUB_BUNDLE_SHA,
            'submodule bundle hash')
    bundle = ARCHIVE / 'c2a-fixtures/submodule-mpvqt/all-refs.bundle'
    git(path, 'bundle', 'verify', str(bundle))
    require(git(path, 'bundle', 'list-heads', str(bundle)).decode().splitlines() == sub['bundle_refs'],
            'submodule bundle refs')
    require(git(path, 'rev-parse', 'HEAD').decode().strip() == sub['head'], 'submodule HEAD')
    require(git(path, 'rev-parse', '--absolute-git-dir').decode().strip().replace('/', '\\').lower() ==
            sub['git_dir'].replace('/', '\\').lower(), 'submodule git directory')
    require(git(path, 'status', '--porcelain=v1', '-z', '--untracked-files=all').hex() == sub['status_hex'],
            'submodule status')
    require(not git(path, 'ls-files', '--others', '--ignored', '--exclude-standard', '-z'), 'submodule ignored')
    require(git(path, 'for-each-ref', '--format=%(objectname) %(refname)').decode().splitlines() == sub['refs'],
            'submodule refs')
    require(git(path, 'stash', 'list').decode().splitlines() == sub['stash'], 'submodule stash')
    index = pathlib.Path(os.fsdecode(git(path, 'rev-parse', '--path-format=absolute', '--git-path', 'index')).strip())
    expected = sub['index']['source_before']
    require(digest(index) == digest(ARCHIVE / 'c2a-fixtures/submodule-mpvqt/index') == expected,
            'submodule index')


def check_worktree(item, sub, bundle_refs):
    root = pathlib.Path(item['worktree'])
    name = root.name
    require(root == ROOT / 'deps/worktrees' / name and name in NAMES, f'unsafe worktree: {root}')
    plain(root, ROOT)
    require(git(root, 'rev-parse', '--show-toplevel').decode().strip().replace('/', '\\').lower() ==
            str(root).lower(), f'root: {root}')
    require(git(root, 'rev-parse', 'HEAD').decode().strip() == item['head'], f'HEAD: {root}')
    require(git(root, 'symbolic-ref', '--short', 'HEAD').decode().strip() == item['branch'], f'branch: {root}')
    require(f"{item['head']} refs/heads/{item['branch']}" in bundle_refs, f'bundle branch: {root}')
    for option, key in (('--absolute-git-dir', 'git_dir'), ('--git-common-dir', 'git_common_dir')):
        args = ('--path-format=absolute', option) if option == '--git-common-dir' else (option,)
        require(git(root, 'rev-parse', *args).decode().strip().replace('/', '\\').lower() ==
                item[key].replace('/', '\\').lower(), f'{key}: {root}')
    status = git(root, 'status', '--porcelain=v1', '-z', '--untracked-files=all').hex()
    require(status == item['status_hex'], f'status: {root}')
    index = pathlib.Path(os.fsdecode(git(root, 'rev-parse', '--path-format=absolute', '--git-path', 'index')).strip())
    expected = item.get('index_sha256', item.get('index_capture', {}).get('source_before'))
    require(digest(index) == digest(ARCHIVE / name / 'index') == expected, f'index: {root}')
    ignored = git(root, 'ls-files', '--others', '--ignored', '--exclude-standard', '-z')
    names = sorted(os.fsdecode(x) for x in ignored.split(b'\0') if x)
    require(names == sorted(x['path'] for x in item['ignored']), f'ignored inventory: {root}')
    require(nested_git(root) == sorted(item['nested_git']), f'nested git: {root}')
    compare_files(root, name, item, 'working')
    compare_files(root, name, item, 'ignored')
    check_submodule(root, item, sub)


def main():
    remaining = sys.argv[1:]
    require(len(remaining) == len(set(remaining)) and set(remaining).issubset(NAMES), 'bad allowlist')
    manifest = ARCHIVE / 'manifest.json'
    require(digest(manifest) == MANIFEST_SHA, 'archive manifest hash')
    data = json.loads(manifest.read_text(encoding='utf-8'))
    items = {pathlib.Path(x['worktree']).name: x for x in data['results']}
    require(set(items) == set(NAMES) and len(data['results']) == len(NAMES), 'manifest allowlist')
    integration = ROOT / 'deps/worktrees/integration'
    require(git(integration, 'rev-parse', 'HEAD').decode().strip() == TARGET, 'integration HEAD')
    git(integration, 'merge-base', '--is-ancestor', SOURCE, TARGET)
    bundle = ROOT / 'deps/cleanup-20261113/pre-cleanup.bundle'
    require(digest(bundle) == BUNDLE_SHA, 'main bundle hash')
    git(ROOT, 'bundle', 'verify', str(bundle))
    bundle_refs = set(git(ROOT, 'bundle', 'list-heads', str(bundle)).decode().splitlines())
    listing = git(ROOT, 'worktree', 'list', '--porcelain').decode().splitlines()
    registered = {line[9:].replace('/', '\\').lower() for line in listing if line.startswith('worktree ')}
    for name in NAMES:
        root = ROOT / 'deps/worktrees' / name
        if name in remaining:
            require(str(root).lower() in registered, f'unregistered: {root}')
            check_worktree(items[name], data['initialized_submodule'], bundle_refs)
        else:
            require(str(root).lower() not in registered and not root.exists(), f'not removed: {root}')
    print(json.dumps({'verified': remaining, 'integration': TARGET}))


if __name__ == '__main__':
    main()
