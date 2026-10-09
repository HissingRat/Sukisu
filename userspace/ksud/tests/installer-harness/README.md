# Actual-source installer regression

Stage the verified resource pair using `scripts/installer/stage.py` first.
From the repository root, link the real resources for RustEmbed and run:

```sh
mkdir -p userspace/ksud/tests/installer-harness/bin
ln -s ../../../bin/aarch64 userspace/ksud/tests/installer-harness/bin/aarch64
cargo build --locked --manifest-path userspace/ksud/tests/installer-harness/Cargo.toml
python3 userspace/ksud/tests/installer-harness/run.py
```

The harness compiles the current production `assets.rs` and `boot_patch.rs`.
Five regression groups verify real embedded module/loader hashes, explicit
helper precedence despite a failing PATH helper, preservation of input bytes,
original-init backup and injection commands, missing KMI/helper errors, local
module override, and Magisk-patched input rejection. The embedded KMI list must
equal `android13-5.15`; an unexpectedly broader or empty resource pack fails.

The magiskboot helper records commands and publishes resource hashes. It does
not implement the Android boot image format. Actual APK execution, real
magiskboot unpack/repack, CPIO content/metadata, and MediaStore export require
the separate device validation. The host build does not compile Android-only
partition, OTA, or flash paths. No device calls occur in this harness.
