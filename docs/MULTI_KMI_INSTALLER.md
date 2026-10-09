# Stock seven-KMI SELinux-hide installer

This ARM64 development installer packages the exact original v4.1.2 list:
`android12-5.10`, `android13-5.10`, `android13-5.15`, `android14-5.15`,
`android14-6.1`, `android15-6.6`, and `android16-6.12`. There is no
`android17-6.18` resource. The chooser reads the actual daemon resource list.
The pinned baseline workflow, original released APK and original release assets
agree on these seven targets. Evidence is in `cache/multi-kmi-validation/`;
the pinned list is in `scripts/installer/stock-v4.1.2.json`.

**This installer supports kernels with 4 KiB memory pages only. Do not use
its patched images with a 16 KiB page kernel.** The reused known-working loader
has 4096-byte ELF LOAD alignment; its load offsets and addresses are not
congruent for 16384-byte pages. APK ZIP alignment does not change this loader
constraint. Both installer themes display this limitation before installation.
The packaging-specific `SUKISU_INSTALLER_4K_ONLY` flag enables that notice and
defaults to false for ordinary builds. The Android 15/16 module builds here
also target 4 KiB pages. Adding the original list does not establish runtime
support for every vendor kernel or page configuration.

The test package remains `com.sukisu.ultra.selinuxtest`, version code `40545`,
version name `4.1.2-selinux-installer-multi-kmi-test`. It uses the same trusted
744-byte development certificate, SHA256
`7cba95aac6bbe0c34fb816789806054b70e69000edbb151fad7c2e5124c2c65d`,
and v2-only APK signing. Its initialization skips shared daemon installation.
The earlier single-KMI APK and its validation artifacts remain preserved.

## Module and loader provenance

All seven genuine AArch64 modules come from the same final SELinux-parity
kernel source inventory, SHA256
`e4865a515e28721e48970de9d4ee0931e16ff5613d73740f3c7e3e60bb67046d`.
The port uses pinned upstream KernelSU commit `df03912f` as documented in
[the parity validation](SELINUX_HIDE_PARITY_VALIDATION.md). Each module was
built against its own KMI's DDK and passed the symbol check against that DDK's
matching `vmlinux`. Only debug information was stripped. No renamed module or
headers-only x86 build is packaged. The original CI stages only AArch64 modules.

Five previously validated matching-KMI builds were reused. The missing
`android13-5.10` and `android14-5.15` modules were genuinely built using their
digest-pinned 20251104 DDKs. Their logs, source inventories, result records,
module hashes and DDK digests are in `cache/multi-kmi/`; the other five records
remain in `cache/parity-validation/`. `modules-provenance.json` describes all
seven inputs. `stage.py` verifies input hashes, AArch64 ELF types, recorded
vermagic, paired package/certificate, source inventories against current source,
and the matching-KMI symbol-check command before staging.

The bundled loader is unchanged:
`723cbf6f62a96cb29d30885c6ce7b73135f73e7e61504e4535b1f9556e59472d`.
Its exact source revision cannot be independently attributed from its stripped
binary. It is reused from the previously working image, as recorded in
[the single-KMI packaging document](SELINUX_HIDE_INSTALLER.md). The alternative
source-built loader is not packaged. No new boot validation is claimed here.

## Repeatable build

Use the Java 21, SDK, NDK 29, Cargo and paired-keystore environment described in
the single-KMI packaging document, then run:

```sh
scripts/installer/build-apk.sh \
  --module-set cache/multi-kmi/modules-provenance.json \
  --loader cache/standalone-patch/loader/ksuinit-original-image \
  --loader-sha256 723cbf6f62a96cb29d30885c6ce7b73135f73e7e61504e4535b1f9556e59472d \
  --loader-provenance cache/standalone-patch/loader/original-loader-provenance.json
```

This selects a distinct version name and `cache/multi-kmi` output directory,
compiles the locked ARM64 daemon with all seven compressed resources, then
builds the ARM64-only Manager. Signature verification enforces the paired
certificate and v2-only signing; APK verification checks exact built-daemon
bytes, the bundled real magiskboot, ABI scope, and source/resource records.
Generated binaries and independent build inputs are not committed.

## Validation scope

All seven resources were independently decoded from the actual final APK/daemon
and matched the independent module hashes and unchanged loader. The ordinary
ADB-shell decoder selects each explicit KMI and uses a recording magiskboot
helper that copies the already-decoded module and loader at the first `unpack`
call, then deliberately fails before any image operation. This verifies actual
daemon selection and extraction without supplying foreign-device images or
attempting to load a foreign module. It is not a boot test of those targets.

The phone is an Android 15 PHB110 with `android13-5.15` and 4 KiB pages. Only
that target received the real UI file-picker patch/export test against a copy
of the original image. The generated image exactly matches the previously verified
`001c990efad51ba9e9c6b95699320956b0769ecfadde5661c47af4fbad51e898`
output. No partition flashing or reboot is part of this round. Root checks use
the already-authorized Termux solely to read partition, daemon, allowlist,
shared-tool and config state; the test app receives no root grant.

## Final results

Final APK: `cache/multi-kmi/SukiSU_4.1.2-selinux-installer-multi-kmi-test_40545-arm64-final.apk`
(7,395,834 bytes, SHA256
`6c53d14486229357443d7dfeba9111933b82c61645e4520215b6cd0091e54429`).
Its embedded daemon SHA256 is
`c01ef8193bd1817fb2161d09ba5d5fbdc508aa6f48a76ed29d4d65f8d5f4c5af`.
The adjacent `.provenance.json` records the exact source and resource inputs.

- Both genuine missing-KMI builds and their matching-vmlinux checks passed;
  all seven source inventories match the final current kernel source.
- Locked ARM64 daemon and Manager release builds, signature verification,
  exact packaged daemon verification, and diff whitespace checks passed.
  Staging rejects a missing stock KMI and an incorrect source inventory hash.
- Independent APK inspection confirmed its package/version, ARM64-only ABI,
  exact seven decoded module payloads, unchanged loader, and actual v2
  certificate bytes. Ordinary UID 2000 runtime extraction independently
  confirmed each KMI-to-module mapping; unsupported Android 17 was rejected
  before the helper ran. Evidence: `decoded-resources.json` and `decoded/`.
- The final APK was freshly installed alongside the official Manager. A pulled
  copy of the installed APK exactly matches the host final artifact. The Miuix
  file-picker flow displayed the 4 KiB notice and exact seven KMI choices,
  patched the copied stock image using `android13-5.15`, and showed
  **镜像修补并导出完成**. MediaStore item `1000001171` was fully published
  (`is_pending=0`). The Material notice compiled and was reviewed, but that
  theme was not separately exercised on the phone.
- The pulled image `cache/multi-kmi-validation/exported-init_boot.img` has
  SHA256 `001c990efad51ba9e9c6b95699320956b0769ecfadde5661c47af4fbad51e898`.
  Its entire 8 MiB is byte-identical to the earlier validated image, so its
  module/loader, CPIO metadata, boot-header and AVB comparison remain identical
  to [the original image validation](IMAGE_PATCH_VALIDATION.md).
- Before/after root records are byte-identical: both init_boot slots, original
  daemon, allowlist and shared executable hashes are unchanged. All captured
  inode/size/mtime/mode/owner and existing symlink metadata also match exactly.
  Feature config and lock remain absent. The input copy, both user originals
  and earlier final single-KMI APK retain their original hashes.
- After host copies were verified, the test APK, task-owned phone input/scripts,
  decoded payloads and single test MediaStore output were removed. The official
  Manager remains installed. The task Colima VM was stopped. No flash, reboot
  or new root grant occurred.

Structured results are retained in `cache/multi-kmi-validation/validation-result.json`,
`decoded-resources.json` and the original-file hash records. Raw test logs,
transcripts, screenshots and UI dumps were removed during the requested cleanup.
The delivered APK/image/module files, build source inventories, matching-vmlinux
build logs required by staging, structured provenance and rollback backups remain
preserved locally. The ignored cleanup manifest is `cache/test-log-cleanup-plan.json`.
