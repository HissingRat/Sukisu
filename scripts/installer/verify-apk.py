#!/usr/bin/env python3
"""Verify packaged binaries (compressed RustEmbed resources require runtime extraction)."""
import argparse
import hashlib
import json
import re
import zipfile
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("apk", type=Path)
parser.add_argument("--daemon", type=Path, required=True)
parser.add_argument("--resources", type=Path, required=True)
parser.add_argument("--signature", type=Path, help="successful apksigner verification output")
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
           "scripts/installer/stage.py", "scripts/installer/stock-v4.1.2.json",
           "scripts/installer/build-apk.sh", "scripts/installer/build-loader.sh"]
signature = None
if args.signature:
    signature = args.signature.read_text()
    assert "Verified using v2 scheme (APK Signature Scheme v2): true" in signature
    assert "Verified using v1 scheme (JAR signing): false" in signature
    assert "Verified using v3 scheme (APK Signature Scheme v3): false" in signature
    certificates = re.findall(r"Signer #\d+ certificate SHA-256 digest: ([0-9a-f]+)", signature)
    assert certificates == ["7cba95aac6bbe0c34fb816789806054b70e69000edbb151fad7c2e5124c2c65d"], "unpaired signing certificate"
with zipfile.ZipFile(args.apk) as archive:
    names = archive.namelist()
    assert all(name.startswith("lib/arm64-v8a/") for name in names if name.startswith("lib/")), "unverified ABI packaged"
    daemon = archive.read("lib/arm64-v8a/libksud.so")
    assert daemon == args.daemon.read_bytes(), "packaged daemon differs from build"
    helper = archive.read("lib/arm64-v8a/libmagiskboot.so")
    assert helper[:4] == b"\x7fELF", "missing executable magiskboot"
    record = {"apk": str(args.apk), "sha256": hashlib.sha256(args.apk.read_bytes()).hexdigest(),
              "size": args.apk.stat().st_size, "daemon_sha256": hashlib.sha256(daemon).hexdigest(),
              "magiskboot_sha256": hashlib.sha256(helper).hexdigest(),
              "verified_signature": signature,
              "staged_resources": json.loads(args.resources.read_text()),
              "source_hashes": {p: hashlib.sha256((root / p).read_bytes()).hexdigest() for p in sources}}
    args.apk.with_suffix(".provenance.json").write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps(record, indent=2))
