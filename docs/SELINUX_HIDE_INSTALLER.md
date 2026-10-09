# SELinux-hide image installer

The later [stock seven-KMI installer](MULTI_KMI_INSTALLER.md) extends this
preserved single-KMI APK with the original v4.1.2 ARM64 resource list.

This phone-targeted development APK packages the tested ARM64 `android13-5.15`
SELinux parity module. Home → Install → Select a file → choose an original
`init_boot.img` → confirm `android13-5.15` produces an image in Downloads.
Selecting a file exports it; this action does not flash a partition or reboot.
Other KMIs are not bundled. The manual `.ko` picker remains available, but a
selected module's device compatibility remains the user's responsibility.

The APK retains package `com.sukisu.ultra.selinuxtest` and disables global daemon
installation to preserve the official Manager and its data. It is signed with the
same development certificate trusted by this test module, using APK v2 only.
It cannot replace the differently signed official Manager as an update.
Both themes show patch/export progress and completion for a selected image;
they keep the existing flash labels for partition installation.

## Resources and provenance

| Resource | SHA256 | Origin |
| --- | --- | --- |
| `android13-5.15_kernelsu.ko` | `b8a03162bb05eb77fd8ecb5790096fa3b73e8b75b347a623f9bae065f38755cd` | Final tested parity module; package filter `com.sukisu.ultra.selinuxtest` |
| `ksuinit` | `723cbf6f62a96cb29d30885c6ce7b73135f73e7e61504e4535b1f9556e59472d` | Loader extracted from a working image copy; identical loader was used with the tested parity module |
| Alternate source-built loader | `b043677d5ddfca97e6f10323956dcc813c0d5f980e6c2630f52f7505dde04b9d` | Unmodified v4.1.2 loader source, Rust 1.99 / NDK 29; not the loader bundled in this APK and not boot-tested |

The working binary's exact source revision cannot be independently attributed
from its stripped contents. Its provenance is intentionally recorded as a
reused known-working loader, rather than a fresh source build. Independent CPIO
records and both loader build records are in `cache/standalone-patch/loader`.
No original image is overwritten during extraction, staging, or patching.

Final APK: `cache/standalone-patch/SukiSU_4.1.2-selinux-installer-test_40545-arm64-final.apk`
(SHA256 `9f9ac0a6a0799b9550693e46e0afebe76b4b34454f09e5c5c74f9c36e6bcbd06`).
Version name/code: `4.1.2-selinux-installer-test` / `40545`.
The adjacent `.provenance.json` sidecar records native-library hashes,
source hashes, resource inputs, and certificate information. Compressed module
and loader resources are embedded in `libksud.so`, rather than separate APK ZIP entries.

## File export behavior

The Manager supplies its own `libmagiskboot.so`, which takes precedence over
PATH tools. For a selected file, input, optional local module, temporary work,
and patched output use a unique app-cache directory. The shell's `TMPDIR` points
there. The patcher does not provision shared `/data/adb` tools for this path.
Android 10+ publishes the completed image through MediaStore Downloads, using a
pending entry and removing it if writing/publication fails. An export error is
reported as a failed operation, even if image patching itself succeeded.
Temporary app-cache copies are cleaned after the operation.

Android 8–9 retains the original shell-to-Downloads behavior, which requires an
authorized root shell because the Manager does not request broad storage access.
No unprivileged legacy export support is claimed. The tested target phone uses
Android 15. Direct partition installation retains its existing root behavior;
it is outside this round's validation and was not authorized here.

## Repeatable packaging

Set `PATH` to include Cargo, `JAVA_HOME` to Java 21, `ANDROID_HOME` to the SDK,
and `ANDROID_NDK_HOME` to NDK `29.0.14206865`. Supply the paired development
keystore through `INSTALLER_KEYSTORE`, `INSTALLER_KEY_ALIAS`,
`INSTALLER_STORE_PASSWORD`, and `INSTALLER_KEY_PASSWORD`.

```sh
scripts/installer/build-apk.sh \
  --module cache/parity-validation/artifacts/android13-5.15_kernelsu-parity-test.ko \
  --module-sha256 b8a03162bb05eb77fd8ecb5790096fa3b73e8b75b347a623f9bae065f38755cd \
  --loader cache/standalone-patch/loader/ksuinit-original-image \
  --loader-sha256 723cbf6f62a96cb29d30885c6ce7b73135f73e7e61504e4535b1f9556e59472d \
  --loader-provenance cache/standalone-patch/loader/original-loader-provenance.json
```

Inputs are external build artifacts. They must be supplied again in a clean
checkout; generated binaries are not committed. `stage.py` verifies both input
hashes, ARM64 ELF types, the module's 5.15 vermagic, matching loader provenance,
and absence of other embedded KMI modules before copying. The build compiles
the ARM64 daemon with `--locked`, then the ARM64-only Manager. `build.rs` now
tracks embedded-resource directories so asset changes invalidate the build.
The script zipaligns, signs with v2 only, verifies the signature, and checks the
APK contains the exact built daemon and a real bundled magiskboot.

To reproduce the alternate source-built loader:

```sh
rustup target add aarch64-unknown-linux-musl
scripts/installer/build-loader.sh
```

This uses the existing locked `rustix` revision
`4a53fbc7cb7a07cabe87125cc21dbc27db316259`, with a per-command Git URL mapping
from the unavailable `Kernel-SU/rustix.git` to `KernelSU2/rustix.git`. It does
not alter manifests, lockfiles, or global Git configuration. The NDK linker uses
API 26 and `-no-pie`; the output is static ARM64 ELF. Other hosts may set
`NDK_HOST_TAG`. Changing to this loader requires new boot validation.

## Checks

- ARM64 release daemon `cargo ndk -t arm64-v8a -P 26 build --release --locked`: passed.
- Manager release build and v2-only signature/exact daemon verification: passed.
- Actual-source patcher harness: 5 groups passed (embedded resources, explicit
  helper selection, input preservation/init backup commands, unsupported KMI,
  local module override, Magisk-patched rejection, and missing helper).
  Its magiskboot services are recording stubs; they do not validate real image formats.
- Actual-source Manager harness: existing 6 feature-state groups plus Downloads
  export byte preservation, pending publication, four failure cleanup paths,
  and empty/unsupported-API refusal passed. Android services are controlled fixtures.
- Rust formatting and diff whitespace: passed. Capped full Clippy passed with
  51 existing warnings; none in the changed patcher/build script. Strict baseline
  Clippy limitations are documented in the parity validation notes.

Raw test logs were removed during the requested cleanup. The validation-time
logs were in `cache/standalone-patch`: `final-title-build.log`, `ksud-release-final.log`,
`installer-regression.log`, `manager-model-export-regression.log`, and `clippy.log`.
Installed-APK extraction and actual stock-image patch/export are independently
validated by the sole device operator; their detailed report belongs alongside
these records. No flashing is part of this installer validation.

See [device image validation](IMAGE_PATCH_VALIDATION.md) for the independent
phone flow, full CPIO/AVB comparison, and shared-state checks. The earlier
`-arm64.apk` candidate and its records remain preserved. RustEmbed includes asset
timestamps in daemon metadata, so restaging identical payload bytes can change
the daemon hash; exact final APK daemon bytes and decoded resource hashes are
verified separately. No bit-for-bit reproducibility across build environments
is claimed.
