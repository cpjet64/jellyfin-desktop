function Install-JobMpvQt {
    param([string]$Workspace, [string]$Qt, [string]$MpvInclude, [string]$MpvLibrary, [string]$Python)
    $revision = & git.exe -C external/mpvqt rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $revision -ne '2ebbd38bf65d01be9c55d673ecd43c8bb9a6fc8e') {
        throw 'Job-local MpvQt source is not the required pinned revision'
    }
    $tools = Join-Path $Workspace 'system-mpvqt'
    New-Item -ItemType Directory -Path $tools | Out-Null
    $archive = Join-Path $tools 'extra-cmake-modules-6.31.0.tar.xz'
    # Published checksum: https://download.kde.org/stable/frameworks/6.31/extra-cmake-modules-6.31.0.tar.xz.sha256
    Get-VerifiedDownload 'https://download.kde.org/stable/frameworks/6.31/extra-cmake-modules-6.31.0.tar.xz' $archive '8F74AF329E4C918358BFE54316F8CF4D530C28444595A3E2FB15075F72877F17'
    $extract = Join-Path $tools 'ecm-source'
    Run-Native $Python @('dev/ci/safe_extract.py', $archive, $extract)
    $ecm = Join-Path $tools 'ecm-install'
    Run-Native 'cmake.exe' @('-S', "$extract/extra-cmake-modules-6.31.0", '-B', "$tools/ecm-build", '-G', 'Ninja', "-DCMAKE_INSTALL_PREFIX=$ecm", '-DBUILD_TESTING=OFF', '-DBUILD_DOC=OFF')
    Run-Native 'cmake.exe' @('--build', "$tools/ecm-build", '--target', 'install', '--parallel', '4')
    Run-Native 'cmake.exe' @('-S', 'dev/windows/ci-ecm-check', '-B', "$tools/ecm-check", '-G', 'Ninja', "-DECM_KDE_MODULE_DIR=$ecm/share/ECM/kde-modules")
    Run-Native 'cmake.exe' @('--build', "$tools/ecm-check", '--target', 'clang-format')
    $source = Join-Path $tools 'mpvqt-source'
    $tree = & git.exe -C external/mpvqt ls-tree -r HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect pinned MpvQt source tree' }
    if (@($tree | Where-Object { $_ -match '^120000\s' }).Count) { throw 'Pinned MpvQt source contains unsupported links' }
    $sourceArchive = Join-Path $tools 'mpvqt-source.tar'
    Run-Native 'git.exe' @('-C', 'external/mpvqt', 'archive', '--format=tar', "--output=$sourceArchive", 'HEAD')
    Run-Native $Python @('dev/ci/safe_extract.py', $sourceArchive, $source)
    if (Test-Path -LiteralPath "$source/.git") { throw 'Copied MpvQt source unexpectedly contains .git' }
    $install = Join-Path $tools 'mpvqt-install'
    $prefixes = "$Qt;$ecm"
    Run-Native 'cmake.exe' @('-S', $source, '-B', "$tools/mpvqt-build", '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=RelWithDebInfo', "-DCMAKE_PREFIX_PATH=$prefixes", "-DCMAKE_INSTALL_PREFIX=$install", "-DLibmpv_INCLUDE_DIRS=$MpvInclude", "-DLibmpv_LIBRARIES=$MpvLibrary", '-DBUILD_SHARED_LIBS=ON', '-DBUILD_EXAMPLES=OFF', '-DBUILD_TESTING=OFF')
    Run-Native 'cmake.exe' @('--build', "$tools/mpvqt-build", '--target', 'install', '--parallel', '4')
    $configs = @(Get-ChildItem -LiteralPath $install -Recurse -Filter 'MpvQtConfig.cmake')
    $libraries = @(Get-ChildItem -LiteralPath $install -Recurse -Filter 'MpvQt*.dll')
    if ($configs.Count -ne 1 -or $libraries.Count -ne 1) { throw 'Installed MpvQt configuration or DLL missing or ambiguous' }
    $env:PATH = "$($libraries[0].DirectoryName);$(Split-Path $MpvLibrary);$env:PATH"
    $script:JobMpvQtConfigDirectory = $configs[0].DirectoryName
    $script:JobMpvQtRuntime = $libraries[0].FullName
    Write-Host "Unmodified MpvQt 1.1.1 installed to job-local prefix: $install"
    Run-Native 'cmake.exe' @('-S', 'dev/windows/ci-mpvqt-check', '-B', "$tools/check-build", '-G', 'Ninja', "-DCMAKE_PREFIX_PATH=$Qt;$install", "-DMpvQt_DIR=$($configs[0].DirectoryName)", "-DEXPECTED_MPVQT_PREFIX=$install", "-DLibmpv_INCLUDE_DIRS=$MpvInclude", "-DLibmpv_LIBRARIES=$MpvLibrary")
}
