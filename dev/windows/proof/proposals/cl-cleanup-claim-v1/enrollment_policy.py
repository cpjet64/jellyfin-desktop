"""Literal enrollment policy; importing this module has no operational effects."""

ROOT = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/reconcile-build-policy-20261113/deps/enrollment-cl-claim-20261114-01'
BASE = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/reconcile-build-policy-20261113/dev/windows/proof/proposals/cl-cleanup-claim-v1'
TOOL = 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/'
NAMES = ('_CL_', '_LINK_', 'APPDATA', 'CL', 'HEALTHY_TEST_SOURCE', 'INCLUDE',
         'JELLYFIN_TEST_OWNED_ROOT', 'LIB', 'LIBPATH', 'LINK', 'LOCALAPPDATA',
         'MPVQT_TEST_SOURCE', 'PATH', 'QT_QPA_PLATFORM', 'SystemRoot', 'TEMP', 'TMP')
FINAL_ENV = 'd9b37c10d4dd5d21ec86562737d8aea7dce17216e56733ae819b5dcfc4f3af01'


def build_policy():
    from build_only.policy import CommandIdentity, Policy

    response = BASE.replace('/', '\\') + r'\cleanup-claim.rsp'
    command = CommandIdentity(
        executable=TOOL + 'cl.exe', executable_sha256='fe251ef50a1545b1b0835ee17b1e785459712b38d79e45b5c1d3d28970a36619',
        executable_size=677776, args=('@' + response,), cwd=ROOT,
        stdout=ROOT + '/compiler.stdout', stderr=ROOT + '/compiler.stderr', response_path=response,
        response_sha256='15d4a13278b38de63943cc4fc8776cbec79d44e4759a968d927ceb65f77de347',
        outputs=(ROOT + '/cleanup_claim.obj', ROOT + '/cleanup_claim-compile.pdb'),
        env_digest=FINAL_ENV, env_names=NAMES)
    return Policy(identity='cleanup-claim-cl-20261114', kind='production', command_form='cl_response',
        source_sha256='80b842ae0d7ad4c3384b9eacfb9c492962febebae449c90a2b18d6b1e4f25915',
        telemetry_path=TOOL + 'vctip.exe', telemetry_sha256='d591358c6aee4b673562ca3b3a15c250e612708394fe3b44a7a7eda2470570a5',
        telemetry_size=335288, telemetry_version='14.44.35229.0',
        telemetry_file_id='2555941978:196608:1477911', command=command, deadline_ms=60000, grace_ms=1000)
