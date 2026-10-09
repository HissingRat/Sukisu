# SELinux hide backport validation

This is an experimental development build based on SukiSU v4.1.2 commit
`ede8a21fb215eccca099db26214308e7f8685a47`, using KernelSU reference
`df03912f70d92ff2aa9762ef82d607033d37e1da`. See
[kernel behavior and limitations](../kernel/SELINUX_HIDE.md). Initial support is
ARM64 Linux 5.15 only; successful compilation is not a device boot test.

The subsequent authorized PHB110 deployment, runtime checks, and completed
restoration are recorded in [device testing](SELINUX_HIDE_DEVICE_TEST.md).

## Kernel build

The release workflow's Linux amd64 DDK was run on macOS ARM64 through a dedicated
Colima VZ/Rosetta VM (`sukisu-build`, 6 CPUs, 12 GiB RAM, 50 GiB disk). The DDK is
pinned by content digest rather than relying on its mutable tag:

The task-created VM was stopped after validation, retaining the image/cache.
Restart it with `colima start --profile sukisu-build` before rerunning the build.

```sh
docker --context colima-sukisu-build run --rm --platform linux/amd64 \
  -v "$PWD:/work" -w /work/kernel \
  ghcr.io/ylarod/ddk@sha256:593b73918e06a3f73b5c97a8b395d6d8bd1dc925d6a9a85384938c74183ed2d9 \
  bash -lc 'export PATH=/opt/ddk/clang/clang-r450784e/bin:$PATH
    git config --global --add safe.directory /work
    CONFIG_KSU=m CONFIG_KSU_MANUAL_SU=y CC=clang make -j6 \
      KSU_GITHUB_VERSION=4.1.2 KSU_GITHUB_VERSION_COMMIT=3358 \
      KSU_VERSION=40545 KSU_VERSION_FULL=v4.1.2-selinux-hide-dev@ede8a21 \
      KSU_EXPECTED_SIZE=744 \
      KSU_EXPECTED_HASH=7cba95aac6bbe0c34fb816789806054b70e69000edbb151fad7c2e5124c2c65d'
```

This uses Clang 14.0.7 (r450784e), the repository's normal LTO/CFI configuration,
modpost, BTF generation, and `check_symbol` against the DDK vmlinux. These build
steps passed. The explicit version overrides distinguish the development build
and avoid network-derived release metadata. Strip a copy using the same image's
`llvm-strip -d`; retain the unstripped module for debugging.

Final stripped artifact: `cache/validation/out/android13-5.15_kernelsu.ko`
(SHA-256 `a3e93751441b9e8f77977e1f3b53209c66baf7235ebd6ac4b31106b27fa670e9`).
The build log is `cache/validation/kernel-build-final.log`. These local artifacts
are ignored by Git.

This module adds trust for the local development Manager certificate above
(744-byte DER certificate) while retaining the official SukiSU certificate.
The certificate is public; the private signing key remains in the local Android
debug keystore. A development-signed Manager is not an in-place update of an
officially signed installation because Android requires matching signing
identities. No installed app or device permissions were changed.

The DDK vmlinux contains all resolved hook symbols and their expected CFI jump
table variants. Its module vermagic is:

```text
5.15.194-android13-5.15.194_r00-dirty SMP preempt mod_unload modversions aarch64
```

## Image provenance and compatibility evidence

The two supplied 8 MiB images were hashed, copied into ignored
`cache/validation/images`, and only those copies were parsed/decompressed. The
original hashes were checked again after analysis and were unchanged:

| Original filename | SHA-256 |
| --- | --- |
| `init_boot.img` | `2d3b65b9ee3d5ac5684efc162111af21fe2f04c14a9e0552b8783582e3f2b8ff` |
| `kernelsu_patched_20260315_134628.img` | `96868cdc024715cfd16ff2d93ab044d07713eda400e15dc49515fe787b7174f6` |

The patched image contains a 318632-byte AArch64 module with SHA-256
`100467a3e0026257c0cb5d81b82fafc47cbf73ab8897ceb41ae4b4a2a74fadc2`, version
`v4.1.2-f39c001e@main`, and no appended Linux module-signature marker. Thus the
supplied module is not built from the requested formal v4.1.2 baseline commit.
Its vermagic matches the DDK build. All 142 shared symbol-version CRCs match the
backport module; the backport additionally imports `__alloc_pages`,
`__free_pages`, `arm64_use_ng_mappings`, `kasan_flag_enabled`, `scnprintf`,
`stop_machine`, `vfree`, and `vmalloc`.

Read-only adb inspection found a PHB110 device running Android 15 with kernel
`5.15.167-android13-8-o-01144-gb3f32e037fab` and a loaded `kernelsu` module. This
does not establish that the supplied image is the image currently running.
Device `/proc/kallsyms` access was denied and shell `su` was unavailable, so
private-symbol presence and vendor structure/CFI compatibility remain unverified.

The initial build-validation stage did not install or flash anything. The later
authorized device stage exercised boot/root, persistence, and query behavior;
see the linked device report for its results and limitations. The ARM64 5.15
early-boot task-work callback pins
the LKM even when hiding stays disabled; ordinary unload is unavailable until
reboot. Existing SELinux status descriptors/mappings retain their selected page
across a feature toggle and require reopening/restarting the app.

## Review fixes

Independent review corrected duplicate SID-table destruction on failed snapshot
initialization, protected snapshot serialization with SELinux's policy mutex,
and moved sleepable policy setup out of a kprobe into init task work. Manager
persistence exceptions now refresh actual state and report an error. Module
pinning and immutable snapshot lifetime avoid freeing callbacks or status pages
that existing callers can still use. Kernel runtime fault injection and race
stress tests have not been performed.

## Userspace and Manager checks

The production Rust feature implementation is exercised by the host regression
harness (no Android device needed):

```sh
cd userspace/ksud
cargo test --manifest-path tests/feature-harness/Cargo.toml
```

The scenario passes and covers pending enable, preserving it during an unrelated
feature save, cancellation before reboot, actual activation on the next simulated
boot, invalid values, old-kernel unsupported results, and genuine ioctl errors.
It also races repeated pending requests with unrelated feature saves; advisory
locking serializes ioctl/config transactions and atomic replacement avoids
partially written configuration files.
It uses mocked ioctls, so it does not validate kernel runtime behavior.

For Android checks use `ANDROID_NDK_HOME` pointing to the installed NDK and run
`cargo ndk -t arm64-v8a check`, `cargo ndk -t arm64-v8a clippy`, then `cargo fmt`.
The original unavailable `Kernel-SU/java-properties` URL was changed to
`KernelSU2/java-properties` with the identical locked commit
`42a4aa941b70ded2dd3be9e9f892471023e70229`; this is a source-location repair,
not a Java-properties dependency upgrade. The baseline lockfile also omitted
already-declared `notify` dependencies; those missing entries were resolved,
while existing locked versions (including `bitflags` 2.10.0) were retained.

Development metadata can be set with environment variables
`SUKISU_VERSION_CODE=40545 SUKISU_VERSION_NAME=4.1.2-selinux-hide-dev` for ksud,
and corresponding Gradle properties `-PSUKISU_VERSION_CODE=40545
-PSUKISU_VERSION_NAME=4.1.2-selinux-hide-dev` for Manager. Normal builds retain
the existing git-derived defaults. Copy the release ksud executable explicitly
to `manager/app/src/main/jniLibs/arm64-v8a/libksud.so` before building Manager;
`cargo ndk -o` does not copy a Rust executable as a shared library.

Android arm64 `cargo ndk check`, both arm64/x86_64 `cargo ndk build --release
--locked`, the host feature regression, and Manager `assembleRelease` passed.
The final Manager uses Java 21 and Android SDK 36 / NDK 29. Both Cargo-built
executables are already stripped; Manager preserves their exact bytes rather
than running a second strip pass.

Full `cargo ndk -t arm64-v8a clippy` does not pass with the installed Rust 1.99:
51 pre-existing warnings remain outside this feature after fixing the touched
feature warning. Running it with `-- --cap-lints warn` completed and reported
those warnings; this is not a clean full-tree lint result. The isolated
production-feature harness passed
`cargo clippy --manifest-path tests/feature-harness/Cargo.toml -- -D warnings`.
`cargo fmt` was run, with unrelated formatting changes kept out of the patch.

Final APK: `cache/artifacts/SukiSU_4.1.2-selinux-hide-dev_40545-experimental.apk`
(SHA-256 `c50a9026dd304e119ddac27a9b64855d74c1ba9e896bb329ae8c1140be4bbbfe`).
Independent verification with `apksigner verify --verbose --print-certs` and
`aapt2 dump badging` confirmed one v2-only signer, no v1/v3/v4 signing,
`com.sukisu.ultra`, version code 40545, and version name
`4.1.2-selinux-hide-dev`. The 744-byte APK certificate exactly matches the
development certificate trusted by the paired module.

The two embedded `libksud.so` entries were compared byte-for-byte against the
final `userspace/ksud/target/<target>/release/ksud` outputs:

| ABI | SHA-256 |
| --- | --- |
| arm64-v8a | `50cf9192dbb93fe2aedf5b25b958e562cf48d9ecef479074919452b0bbe91358` |
| x86_64 | `fcd728b71403f1241e8dbb37cb7ce544d373bbcab981f6aa2a52ff74704ad18a` |

Final `git diff --check` passed. Package verification and mocked feature tests
alone do not establish Android UI or privileged device runtime correctness;
the separate device report records the subsequent runtime evidence.
