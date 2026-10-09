#!/usr/bin/env python3
"""Execute the actual patcher with recording magiskboot services (not image-format emulation).

Real unpack/repack correctness and installed-APK resource extraction are checked
separately on the phone, using its packaged magiskboot and an original image copy.
"""
import hashlib
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
BIN = HERE / "target/debug/ksud-installer-tests"
ASSETS = ROOT / "userspace/ksud/bin/aarch64"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


with tempfile.TemporaryDirectory(prefix="installer-regression-") as directory:
    work = Path(directory)
    image = work / "original init_boot.img"
    image.write_bytes(b"immutable stock input fixture")
    original = digest(image)
    record = work / "calls.jsonl"
    helper = work / "bundled magiskboot"
    helper.write_text(f"#!{sys.executable}\n" + '''import sys, os, json, hashlib
from pathlib import Path
args=sys.argv[1:]
with open(os.environ['PATCH_CALLS'],'a') as log: log.write(json.dumps(args)+'\\n')
if args[0]=='unpack':
    Path('ramdisk.cpio').write_bytes(b'ramdisk fixture')
elif args[0]=='cpio':
    command=args[2]
    if command=='test': sys.exit(int(os.environ.get('MAGISK_PATCHED','0')))
    if command=='exists kernelsu.ko': sys.exit(1)
elif args[0]=='repack':
    result={name:hashlib.sha256(Path(name).read_bytes()).hexdigest() for name in ['init','kernelsu.ko']}
    result['input']=hashlib.sha256(Path(args[1]).read_bytes()).hexdigest()
    Path('new-boot.img').write_text(json.dumps(result))
else: sys.exit(89)
''')
    helper.chmod(0o755)
    path_tools = work / "path-tools"
    path_tools.mkdir()
    sentinel = path_tools / "magiskboot"
    sentinel.write_text("#!/bin/sh\nexit 91\n")
    sentinel.chmod(0o755)
    env = dict(os.environ, PATH=str(path_tools) + os.pathsep + os.environ['PATH'],
               PATCH_CALLS=str(record), TMPDIR=str(work))
    output = work / "output"
    output.mkdir()
    command = [str(BIN), '-b', str(image), '--magiskboot', str(helper), '-o', str(output),
               '--out-name', 'patched.img']

    def run(extra, success=True, extra_env=None):
        result = subprocess.run(command + extra, env=env | (extra_env or {}), capture_output=True, text=True)
        assert (result.returncode == 0) == success, result.stdout + result.stderr
        assert digest(image) == original, "input image mutated"
        return result

    run(['--kmi', 'android13-5.15'])
    result = json.loads((output / 'patched.img').read_text())
    assert result == {'init': digest(ASSETS / 'ksuinit'),
                      'kernelsu.ko': digest(ASSETS / 'android13-5.15_kernelsu.ko'), 'input': original}
    calls = [json.loads(line) for line in record.read_text().splitlines()]
    assert any(call[-1] == 'mv init init.real' for call in calls)
    assert any(call[-1] == 'add 0755 init init' for call in calls)
    assert any(call[-1] == 'add 0755 kernelsu.ko kernelsu.ko' for call in calls)
    print('PASS embedded KMI+loader, original-init backup, exact helper over PATH, immutable input')

    (output / 'patched.img').unlink()
    missing = run(['--kmi', 'android99-9.99'], False)
    assert 'Failed to copy android99-9.99_kernelsu.ko' in missing.stderr
    assert not (output / 'patched.img').exists()
    print('PASS unsupported KMI fails without exported output')

    custom = work / 'custom.ko'
    custom.write_bytes(b'custom module fixture')
    run(['-m', str(custom)])
    assert json.loads((output / 'patched.img').read_text())['kernelsu.ko'] == digest(custom)
    print('PASS manual module selection still uses real embedded loader')

    (output / 'patched.img').unlink()
    run(['--kmi', 'android13-5.15'], False, {'MAGISK_PATCHED': '1'})
    assert not (output / 'patched.img').exists()
    print('PASS Magisk-patched input rejected')

    missing_command = command.copy()
    missing_command[missing_command.index('--magiskboot') + 1] = str(work / 'missing')
    missing_tool = subprocess.run(missing_command, env=env, capture_output=True)
    assert missing_tool.returncode != 0
    print('PASS missing explicit helper fails')

print('5 installer regression groups passed; magiskboot services are stubs, not real image validation.')
