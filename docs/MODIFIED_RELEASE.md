# SukiSU Ultra v4.1.2 modified

The permanent app uses package `com.sukisu.ultra`, display name **SukiSU Ultra**,
version name `v4.1.2 modified` and version code `40545`. The home page displays
`v4.1.2 modified (40545)`. Normal production daemon installation remains enabled.
The differently signed old app must be uninstalled before this release is
installed; preserve its data and a rollback APK/image first.

This release retains all seven original v4.1.2 ARM64 KMIs and the tested
SELinux-hide changes. Each module is rebuilt for the production package and
the actual release signing certificate: DER size `903`, SHA256
`3a469fbd720bb8db3027ae68833980f100bffa7ca311d4e0d7f74aba0390326f`.
The exact public pairing is in `scripts/installer/release-v4.1.2.json`.
Test-package modules are rejected even when their package name shares a prefix
with the production name. Their independent source inventories and matching
DDK/vmlinux checks are preserved in local build provenance.

**Only 4 KiB page kernels are supported.** The unchanged known-working loader
is `723cbf6f62a96cb29d30885c6ce7b73135f73e7e61504e4535b1f9556e59472d`;
it cannot run on 16 KiB page kernels. Both installer themes show this limitation.
See [the earlier multi-KMI validation](MULTI_KMI_INSTALLER.md) for the retained
loader provenance and scope of prior tests.

## Secure release build

Use the existing Java 21, SDK, NDK 29 and Cargo environment. Keep a private
JSON signing config outside Git, with permissions `0600` and these fields:
`keystore_file`, `keystore_password`, `key_alias`, and `key_password`. Signing
values are read locally and passed through process environment variables;
they are not included in commands, source, logs or provenance.

```sh
python3 scripts/installer/build-modified-release.py \
  --signing-config cache/modified-release/signing.local.json \
  --module-set cache/modified-release/modules-provenance.json
```

The wrapper exports and checks the actual public certificate before building,
verifies every module's release trust pairing, compiles the locked ARM64 daemon,
and signs the APK with v2 only. It checks the real APK's package, label, version,
signature and exact native daemon bytes, and verifies the original keystore
remained unchanged. The final APK is
`cache/modified-release/SukiSU_v4.1.2-modified_40545-arm64-release.apk`.
Its adjacent `.provenance.json` records hashes, public pairing, the actual
source commit and whether the source was dirty. The final export is built after
the feature commit has been pushed, from clean committed source. Generated
APKs, native binaries, private signing inputs and device backups remain ignored.

The previous single-KMI and seven-KMI test APKs remain separate preserved
artifacts. Their explicit test build mode still exists for reproducing those
records; it does not define this permanent release's package or trust pairing.

## Device deployment

Deployment uses the user's manual app flow: select a copy of the original
`init_boot.img`, confirm the current `android13-5.15` KMI, and patch/export.
The resulting image is saved as a new file alongside the immutable originals.
Current boot partitions, shared state and old app data must be backed up before
the authorized uninstall and flash. Flash/reboot verification and permanent
installation are handled by the sole device operator. The final device record
must distinguish this new release certificate/module from the older test build.
