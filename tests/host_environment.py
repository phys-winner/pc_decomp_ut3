"""Installed Windows C toolchain for the port's native boundary fixtures."""
import os
from pathlib import Path
import subprocess

CLANG = Path(os.environ['LOCALAPPDATA']) / 'Android/Sdk/ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64/bin/clang-cl.exe'


def environment(arch):
    assert arch in ('x86', 'x64')
    vswhere = Path(r'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe')
    installation = Path(subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-property', 'installationPath'], text=True).strip())
    msvc = sorted((installation / 'VC/Tools/MSVC').iterdir())[-1]
    sdk = Path(r'C:\Program Files (x86)\Windows Kits\10')
    version = sorted((sdk / 'Lib').iterdir())[-1].name
    env = os.environ.copy()
    env['PATH'] = str(msvc / 'bin/Hostx64' / arch) + os.pathsep + env['PATH']
    env['INCLUDE'] = ';'.join(map(str, [msvc / 'include', sdk / f'Include/{version}/ucrt', sdk / f'Include/{version}/shared', sdk / f'Include/{version}/um']))
    env['LIB'] = ';'.join(map(str, [msvc / f'lib/{arch}', sdk / f'Lib/{version}/ucrt/{arch}', sdk / f'Lib/{version}/um/{arch}']))
    return env, msvc / 'bin/Hostx64' / arch / 'link.exe'
