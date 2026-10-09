# Selected-image patch/export validation

Completed on the PHB110 Android 15 device without flashing, rebooting, granting
root to the test app, or replacing the official Manager. Packaging and repeatable
build commands are in [SELINUX_HIDE_INSTALLER.md](SELINUX_HIDE_INSTALLER.md).

## Final artifacts

| Artifact | SHA256 |
| --- | --- |
| `cache/standalone-patch/SukiSU_4.1.2-selinux-installer-test_40545-arm64-final.apk` | `9f9ac0a6a0799b9550693e46e0afebe76b4b34454f09e5c5c74f9c36e6bcbd06` |
| `cache/installer-validation/exported-init_boot-final.img` | `001c990efad51ba9e9c6b95699320956b0769ecfadde5661c47af4fbad51e898` |
| Embedded final ARM64 daemon | `00c6f209c85dcc5eac90cb8aa8398d53ad94a8d426298dfbc51e5fcd9df666eb` |

The APK is ARM64-only, version `4.1.2-selinux-installer-test` / `40545`, package
`com.sukisu.ultra.selinuxtest`. Its v2-only signature and 744-byte certificate
were independently verified; certificate SHA256 is
`7cba95aac6bbe0c34fb816789806054b70e69000edbb151fad7c2e5124c2c65d`.
The embedded daemon exactly matches the final locked release output. Its
compressed resources were verified from the actual generated image, not by
searching compressed APK bytes.

## Actual phone flow

The final APK was installed alongside the official app. In the default Miuix UI:
Home → Install → Select a file → copied original `init_boot.img` → Next → confirm
`android13-5.15`. The KMI chooser advertised only that bundled target. The
system picker used OnePlus 11 → Download → the dedicated test folder; its
Downloads-provider view did not initially index the adb-copied input.

Patching ran against app-cache copies and exported through MediaStore Downloads.
The final result title was **镜像修补并导出完成**. The output existed at
`Download/kernelsu_patched_1791546060807.img`, MediaStore record `1000001168`,
and was pulled to the host. The prior candidate was also exercised; both outputs
were byte-identical. No flash/reboot action was invoked or offered after export.
The Material flow uses the same backend; its selected-image title conditions
were reviewed and compiled, but were not separately exercised on the phone.

## Image verification

Original stock input SHA256:
`2d3b65b9ee3d5ac5684efc162111af21fe2f04c14a9e0552b8783582e3f2b8ff`.
Independent magiskboot unpacking and a CPIO parser confirmed:

- 16 stock entries became 18. Stock `init` bytes were preserved as `init.real`.
- New `init` is the known-working loader
  `723cbf6f62a96cb29d30885c6ce7b73135f73e7e61504e4535b1f9556e59472d`.
- `kernelsu.ko` is the tested module
  `b8a03162bb05eb77fd8ecb5790096fa3b73e8b75b347a623f9bae065f38755cd`.
- Other stock contents, modes, owners, timestamps, link counts and device fields
  are unchanged. CPIO inode numbers were renumbered during insertion; inode
  identity relationships were preserved.
- The 8 MiB image's boot-header changes are limited to ramdisk length. Its
  existing 832-byte AVB metadata is identical; footer offsets reflect the new
  ramdisk size. The original and output show the same pre-existing ASN.1 warning.

The whole output is byte-identical to the image successfully boot-tested in
[the prior parity round](SELINUX_HIDE_PARITY_VALIDATION.md). This round generated
and inspected those bytes through the app; it did not flash them again.
The reused loader's exact source revision is not independently attributable;
its verified image provenance is recorded in the packaging document. The
optional freshly source-built loader is not bundled or newly boot-tested.

## Unchanged device state and cleanup

Before and after both exports, root read-only checks confirmed:

- Original slot A: `96868cdc024715cfd16ff2d93ab044d07713eda400e15dc49515fe787b7174f6`.
- Untouched slot B: `c3c60246b0faa0abf46f31f37c6a2d611c2418d6d0b74f483be2357876c06b6d`.
- Original daemon, allowlist and all five shared executable hashes unchanged.
  Their inode, size, mtime, mode, ownership and existing symlink metadata also
  match, excluding even identical-byte shared-tool rewrites.
- Feature config and lock remain absent. The selected input copy and both user
  originals remain unchanged; the official APK backup hash remains unchanged.

After verified host copies, the two MediaStore test outputs, test input/export
folder, temporary scripts/UI dump and test APK were removed. The official
package remains installed. The task Colima VM was stopped after image review.
The final APK and exported image remain on the host for delivery.

Evidence is in `cache/installer-validation/`: `apk-independent-final.json`,
`apk-signature-final.txt`, `ui-success-final.xml`, `image-comparison.json`,
`baseline.txt`, `metadata-before.txt`, `device-exports/final-state.txt`,
`device-exports/metadata-after.txt`, `cleanup-check.txt`, and
`originals-after.sha256`. The local `compare_images.py` rechecks decoded payloads,
stock-entry metadata, header changes and AVB metadata against the saved unpacked
copies. This validates the supplied boot-v4/LZ4-legacy image, not every vendor
image format or every Android version. No non-5.15 KMI is bundled in this APK.
