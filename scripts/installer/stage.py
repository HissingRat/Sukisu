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
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STOCK = json.loads((Path(__file__).parent / "stock-v4.1.2.json").read_text())
MANAGER = "com.sukisu.ultra.selinuxtest"
CERT = "7cba95aac6bbe0c34fb816789806054b70e69000edbb151fad7c2e5124c2c65d"


def checked_file(path, expected, elf_type):
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != expected:
        raise ValueError(f"SHA256 mismatch: {path}: {digest}")
    if data[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<HH", data, 16) != (elf_type, 183):
        raise ValueError(f"not the required AArch64 ELF type {elf_type}: {path}")
    return {"source": str(path.resolve()), "sha256": digest, "size": len(data)}


def stock_module_set(path):
    baseline = subprocess.check_output(["git", "show", f"{STOCK['base_commit']}:{STOCK['workflow']}"],
                                       cwd=ROOT, text=True)
    names = re.findall(r"^\s+- (android\d+-\d+\.\d+)$", baseline, re.MULTILINE)
    if names != STOCK["kmis"]:
        raise ValueError("recorded stock KMI list does not match pinned v4.1.2 workflow")
    entries = json.loads(path.read_text())
    if sorted(entry["kmi"] for entry in entries) != sorted(names):
        raise ValueError("module set must contain exactly the seven stock v4.1.2 KMIs, without duplicates")
    kernel = ROOT / "kernel"
    current_sources = {
        str(file.relative_to(kernel)): hashlib.sha256(file.read_bytes()).hexdigest()
        for file in kernel.rglob("*")
        if file.is_file() and (file.suffix in [".c", ".h", ".S"] or file.name in ["Makefile", "Kbuild"])
        and not file.name.endswith(".mod.c")
        and not any(part.startswith(".") for part in file.relative_to(kernel).parts)
    }
    resources = {}
    for entry in entries:
        kmi = entry["kmi"]
        source = ROOT / entry["module"]
        metadata = checked_file(source, entry["sha256"], 1)
        version = kmi.split("-", 1)[1]
        matches = re.findall(rb"vermagic=([^\0]+)", source.read_bytes())
        if len(matches) != 1 or not matches[0].startswith((version + ".").encode()):
            raise ValueError(f"wrong kernel vermagic for {kmi}")
        if (entry["build"]["kmi"] != kmi or entry["build"]["status"] != 0
                or entry["manager_package"] != MANAGER or entry["cert_size"] != 744
                or entry["cert_sha256"] != CERT or not entry["matching_vmlinux_symbol_check"]):
            raise ValueError(f"unverified or differently paired module: {kmi}")
        source_manifest = ROOT / entry["source_manifest"]
        source_files = json.loads(source_manifest.read_text())
        source_hash = hashlib.sha256(json.dumps(source_files, sort_keys=True).encode()).hexdigest()
        if source_hash != entry["build"]["source_hash"]:
            raise ValueError(f"source inventory hash does not match build record: {kmi}")
        if current_sources != source_files:
            raise ValueError(f"build source inventory differs from current kernel: {kmi}")
        build_log = ROOT / entry["build_log"]
        log = build_log.read_text()
        if not re.search(rf"^\./check_symbol kernelsu\.ko /[^\n]*\b{re.escape(kmi)}/vmlinux$", log, re.MULTILINE):
            raise ValueError(f"missing matching-KMI vmlinux symbol check in build log: {kmi}")
        if matches[0].decode() != entry["vermagic"]:
            raise ValueError(f"module vermagic does not match independent record: {kmi}")
        data = source.read_bytes()
        if MANAGER.encode() not in data or CERT.encode() not in data:
            raise ValueError(f"module binary does not contain the paired Manager/certificate: {kmi}")
        metadata["provenance"] = entry
        metadata["source_manifest_sha256"] = hashlib.sha256(source_manifest.read_bytes()).hexdigest()
        metadata["build_log_sha256"] = hashlib.sha256(build_log.read_bytes()).hexdigest()
        resources[f"{kmi}_kernelsu.ko"] = metadata
    return resources


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modules = parser.add_mutually_exclusive_group(required=True)
    modules.add_argument("--module", type=Path)
    modules.add_argument("--module-set", type=Path, help="independent seven-KMI module provenance JSON")
    parser.add_argument("--module-sha256")
    parser.add_argument("--loader", type=Path, required=True)
    parser.add_argument("--loader-sha256", required=True)
    parser.add_argument("--loader-provenance", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()
    if args.module_set:
        resources = stock_module_set(args.module_set)
        kmis = STOCK["kmis"]
    else:
        if not args.module_sha256:
            parser.error("--module-sha256 is required with --module")
        module = checked_file(args.module, args.module_sha256, 1)
        if b"vermagic=5.15." not in args.module.read_bytes():
            raise ValueError("module does not have the required 5.15 vermagic")
        resources = {"android13-5.15_kernelsu.ko": module}
        kmis = ["android13-5.15"]
    loader = checked_file(args.loader, args.loader_sha256, 2)
    loader["provenance"] = json.loads(args.loader_provenance.read_text())
    if loader["provenance"].get("sha256") != loader["sha256"]:
        raise ValueError("loader provenance hash does not match staged loader")
    destination = ROOT / "userspace/ksud/bin/aarch64"
    unexpected = [p.name for p in destination.glob("*_kernelsu.ko") if p.name not in resources]
    if unexpected:
        raise ValueError(f"other KMI resources would broaden this build: {unexpected}")
    resources["ksuinit"] = loader
    for name, metadata in resources.items():
        target = destination / name
        if not target.exists() or hashlib.sha256(target.read_bytes()).hexdigest() != metadata["sha256"]:
            shutil.copyfile(metadata["source"], target)
        (destination / name).chmod(0o755)
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps({"architecture": "arm64-v8a", "supported_kmis": kmis,
                                         "resources": resources}, indent=2) + "\n")
    print(args.manifest)


if __name__ == "__main__":
    main()
