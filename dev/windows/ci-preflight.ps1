function Write-NativeInventory {
    param([string]$Root, [string]$Workspace, [string]$Mode)
    $drive = Get-PSDrive -Name ([IO.Path]::GetPathRoot($Root).Substring(0, 1))
    Write-Host "Runner OS=$([Environment]::OSVersion.VersionString) architecture=$env:PROCESSOR_ARCHITECTURE free_bytes=$($drive.Free)"
    Write-Host "CI job=$env:CI_JOB_ID runner=$env:CI_RUNNER_ID runner_arch=$env:CI_RUNNER_EXECUTABLE_ARCH"
    Write-Host "Workspace=$Root job_workspace=$Workspace mode=$Mode user_interactive=$([Environment]::UserInteractive)"
    foreach ($name in @('git.exe', 'python.exe', 'curl.exe', 'tar.exe', 'cmake.exe', 'ctest.exe', 'ninja.exe', 'node.exe', '7z.exe', 'ISCC.exe')) {
        $tool = Find-OptionalTool $name
        if ($tool) { Write-Host "Tool ${name}: $tool" } else { Write-Host "Tool ${name}: not on PATH" }
    }
    foreach ($name in @('cmake.exe', 'ctest.exe', 'ninja.exe', 'node.exe', 'python.exe')) {
        $tool = Find-OptionalTool $name
        if ($tool) {
            $version = & $tool --version 2>&1 | Select-Object -First 1
            Write-Host "Version ${name}: $version"
        }
    }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        Write-Host "MSVC installation: $vs"
        if ($vs) {
            Write-Host "MSVC x64 environment exists: $(Test-Path -LiteralPath "$vs/VC/Auxiliary/Build/vcvars64.bat")"
            $vcVersion = Join-Path $vs 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt'
            if (Test-Path -LiteralPath $vcVersion) { Write-Host "MSVC tool version: $((Get-Content -LiteralPath $vcVersion -TotalCount 1))" }
            $redist = Get-ChildItem -Path "$vs/VC/Redist/MSVC/*/x64/Microsoft.VC*.CRT" -Directory -ErrorAction SilentlyContinue | Select-Object -First 1
            Write-Host "MSVC redistributable: $($redist.FullName)"
        }
    } else { Write-Host 'MSVC discovery tool: MISSING' }
    foreach ($name in @('WindowsSdkDir', 'WindowsSDKVersion')) {
        $value = [Environment]::GetEnvironmentVariable($name, 'Process')
        Write-Host "SDK ${name}: $(if ($value) { $value } else { 'not yet selected' })"
    }
    Write-Host "Qt expected job prefix=$Workspace/qt/6.9.3/msvc2022_64 modules=Core,Quick,WebEngine,WebChannel,Positioning"
    Write-Host "mpv expected job paths=$Root/build/mpv/include and $Root/build/mpv/libmpv-2.dll.lib"
    Write-Host "MpvQt system mode: unmodified pinned source, job-local installed shared package; availability established after bootstrap"
    Write-Host 'Display: hidden-desktop and real rendering capabilities must be proven by required GUI tests'
}
