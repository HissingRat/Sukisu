#!/usr/bin/env python3
"""Stage hash-verified ARM64 installer resources before compiling RustEmbed.

The caller supplies hashes from the independent module/loader build records.
No download, module compilation, signing, or device access happens here.
"""
import argparse
import hashlib
import json
import shutil
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def checked_file(path, expected, elf_type):
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != expected:
        raise ValueError(f"SHA256 mismatch: {path}: {digest}")
    if data[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<HH", data, 16) != (elf_type, 183):
        raise ValueError(f"not the required AArch64 ELF type {elf_type}: {path}")
    return {"source": str(path.resolve()), "sha256": digest, "size": len(data)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--module", type=Path, required=True)
    parser.add_argument("--module-sha256", required=True)
    parser.add_argument("--loader", type=Path, required=True)
    parser.add_argument("--loader-sha256", required=True)
    parser.add_argument("--loader-provenance", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()
    module = checked_file(args.module, args.module_sha256, 1)
    loader = checked_file(args.loader, args.loader_sha256, 2)
    loader["provenance"] = json.loads(args.loader_provenance.read_text())
    if loader["provenance"].get("sha256") != loader["sha256"]:
        raise ValueError("loader provenance hash does not match staged loader")
    if b"vermagic=5.15." not in args.module.read_bytes():
        raise ValueError("module does not have the required 5.15 vermagic")
    destination = ROOT / "userspace/ksud/bin/aarch64"
    unexpected = [p.name for p in destination.glob("*_kernelsu.ko") if p.name != "android13-5.15_kernelsu.ko"]
    if unexpected:
        raise ValueError(f"other KMI resources would broaden this build: {unexpected}")
    resources = {"android13-5.15_kernelsu.ko": module, "ksuinit": loader}
    for name, metadata in resources.items():
        shutil.copyfile(metadata["source"], destination / name)
        (destination / name).chmod(0o755)
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps({"architecture": "arm64-v8a", "supported_kmis": ["android13-5.15"],
                                         "resources": resources}, indent=2) + "\n")
    print(args.manifest)


if __name__ == "__main__":
    main()
