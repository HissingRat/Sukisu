#!/usr/bin/env python3
"""Build the production-package installer using a private local signing config.

The JSON config contains keystore_file, keystore_password, key_alias and
key_password. Keep it outside Git (for example under ignored cache/) with mode
0600. Only public certificate metadata is recorded in build provenance.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
PAIRING = Path(__file__).parent / "release-v4.1.2.json"
LOADER_HASH = "723cbf6f62a96cb29d30885c6ce7b73135f73e7e61504e4535b1f9556e59472d"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--signing-config", type=Path, required=True)
    parser.add_argument("--module-set", type=Path, default=ROOT / "cache/modified-release/modules-provenance.json")
    args = parser.parse_args()
    config = json.loads(args.signing_config.read_text())
    required = ["keystore_file", "keystore_password", "key_alias", "key_password"]
    if not all(isinstance(config.get(key), str) and config[key] for key in required):
        parser.error("private signing config must contain all four nonempty string fields")
    if args.signing_config.stat().st_mode & 0o077:
        parser.error("private signing config must have mode 0600")
    keystore = Path(config["keystore_file"]).resolve()
    before = hashlib.sha256(keystore.read_bytes()).hexdigest()
    pairing = json.loads(PAIRING.read_text())
    environment = os.environ.copy()
    environment.update({
        "INSTALLER_PROFILE": "modified-release",
        "INSTALLER_KEYSTORE": str(keystore),
        "INSTALLER_STORE_PASSWORD": config["keystore_password"],
        "INSTALLER_KEY_ALIAS": config["key_alias"],
        "INSTALLER_KEY_PASSWORD": config["key_password"],
    })
    output = ROOT / "cache/modified-release"
    output.mkdir(parents=True, exist_ok=True)
    certificate = output / "release-certificate.der"
    certificate.unlink(missing_ok=True)
    keytool = Path(environment["JAVA_HOME"]) / "bin/keytool"
    result = subprocess.run([
        str(keytool), "-exportcert", "-keystore", str(keystore), "-alias", config["key_alias"],
        "-storepass:env", "INSTALLER_STORE_PASSWORD", "-file", str(certificate),
    ], env=environment, capture_output=True)
    if result.returncode:
        parser.error("certificate export failed; check the trusted private signing config")
    public = certificate.read_bytes()
    if len(public) != pairing["cert_size"] or hashlib.sha256(public).hexdigest() != pairing["cert_sha256"]:
        parser.error("keystore certificate does not match the public release pairing")
    subprocess.run([
        str(ROOT / "scripts/installer/build-apk.sh"), "--module-set", str(args.module_set.resolve()),
        "--pairing", str(PAIRING),
        "--loader", str(ROOT / "cache/standalone-patch/loader/ksuinit-original-image"),
        "--loader-sha256", LOADER_HASH,
        "--loader-provenance", str(ROOT / "cache/standalone-patch/loader/original-loader-provenance.json"),
    ], cwd=ROOT, env=environment, check=True)
    if hashlib.sha256(keystore.read_bytes()).hexdigest() != before:
        raise RuntimeError("original keystore changed during the build")


if __name__ == "__main__":
    main()
