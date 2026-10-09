#!/usr/bin/env python3
"""Verify packaged binaries (compressed RustEmbed resources require runtime extraction)."""
import argparse
import hashlib
import json
import re
import subprocess
import zipfile
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("apk", type=Path)
parser.add_argument("--daemon", type=Path, required=True)
parser.add_argument("--resources", type=Path, required=True)
parser.add_argument("--signature", type=Path, help="successful apksigner verification output")
parser.add_argument("--pairing", type=Path, help="public release pairing metadata, if not the preserved test profile")
parser.add_argument("--aapt2", type=Path, help="SDK aapt2 for actual package/version/label verification")
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
sources = ["userspace/ksud/build.rs", "userspace/ksud/src/boot_patch.rs", "manager/app/build.gradle.kts",
           "manager/app/src/main/java/com/sukisu/ultra/ui/util/KsuCli.kt",
           "manager/app/src/main/java/com/sukisu/ultra/ui/util/PatchedImageExport.kt",
           "manager/app/src/main/java/com/sukisu/ultra/ui/screen/flash/FlashMaterial.kt",
           "manager/app/src/main/java/com/sukisu/ultra/ui/screen/flash/FlashMiuix.kt",
           "manager/app/src/main/java/com/sukisu/ultra/ui/screen/install/InstallMaterial.kt",
           "manager/app/src/main/java/com/sukisu/ultra/ui/screen/install/InstallMiuix.kt",
           "manager/app/src/main/res/values/strings.xml", "manager/app/src/main/res/values-zh-rCN/strings.xml",
           "scripts/installer/stage.py", "scripts/installer/verify-apk.py", "scripts/installer/stock-v4.1.2.json",
           "scripts/installer/build-apk.sh", "scripts/installer/build-loader.sh",
           "scripts/installer/build-modified-release.py", "scripts/installer/release-v4.1.2.json",
           "manager/build.gradle.kts"]
pairing = json.loads(args.pairing.read_text()) if args.pairing else {
    "manager_package": "com.sukisu.ultra.selinuxtest", "cert_size": 744,
    "cert_sha256": "7cba95aac6bbe0c34fb816789806054b70e69000edbb151fad7c2e5124c2c65d"}
resources = json.loads(args.resources.read_text())
if args.pairing:
    assert all(resources["pairing"][key] == pairing[key]
               for key in ["manager_package", "cert_size", "cert_sha256"]), "staged modules use a different trust pairing"
signature = None
identity = None
if args.aapt2:
    badging = subprocess.check_output([str(args.aapt2), "dump", "badging", str(args.apk)], text=True)
    package = re.search(r"^package: name='([^']+)' versionCode='([^']+)' versionName='([^']+)'", badging, re.MULTILINE)
    assert package and package[1] == pairing["manager_package"], "wrong APK package"
    identity = {"package": package[1], "version_code": int(package[2]), "version_name": package[3]}
    if "version_name" in pairing:
        assert identity["version_name"] == pairing["version_name"] and identity["version_code"] == pairing["version_code"]
        assert f"application-label:'{pairing['app_label']}'" in badging, "wrong APK label"
if args.signature:
    signature = args.signature.read_text()
    assert "Verified using v2 scheme (APK Signature Scheme v2): true" in signature
    assert "Verified using v1 scheme (JAR signing): false" in signature
    assert "Verified using v3 scheme (APK Signature Scheme v3): false" in signature
    certificates = re.findall(r"Signer #\d+ certificate SHA-256 digest: ([0-9a-f]+)", signature)
    assert certificates == [pairing["cert_sha256"]], "unpaired signing certificate"
with zipfile.ZipFile(args.apk) as archive:
    names = archive.namelist()
    assert all(name.startswith("lib/arm64-v8a/") for name in names if name.startswith("lib/")), "unverified ABI packaged"
    daemon = archive.read("lib/arm64-v8a/libksud.so")
    assert daemon == args.daemon.read_bytes(), "packaged daemon differs from build"
    helper = archive.read("lib/arm64-v8a/libmagiskboot.so")
    assert helper[:4] == b"\x7fELF", "missing executable magiskboot"
    record = {"apk": str(args.apk), "sha256": hashlib.sha256(args.apk.read_bytes()).hexdigest(),
              "source_git_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
              "source_git_dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=root, text=True).strip()),
              "size": args.apk.stat().st_size, "daemon_sha256": hashlib.sha256(daemon).hexdigest(),
              "magiskboot_sha256": hashlib.sha256(helper).hexdigest(),
              "verified_signature": signature,
              "verified_identity": identity,
              "staged_resources": resources,
              "source_hashes": {p: hashlib.sha256((root / p).read_bytes()).hexdigest() for p in sources}}
    args.apk.with_suffix(".provenance.json").write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps(record, indent=2))
