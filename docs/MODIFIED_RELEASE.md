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

## Completed deployment and persistence check

The final APK was exported after commit
`2abdc055fb7ca815f11f24f46446fc3672bff8c6` was pushed. Its provenance records
that exact build commit and clean source. APK SHA256:
`3c684d809908db529b285e1660852201bfc35fa785f97936696180da771e296d`
(7,395,834 bytes). A matching copy is saved in the user's `op11` Downloads
folder as `SukiSU_v4.1.2-modified_40545-arm64-release.apk`.

Deployment completed on the PHB110 running Android 15,
`5.15.167-android13-8-o-01144-gb3f32e037fab`, with 4 KiB pages:

- The existing installed APK, private app data, both current init_boot slots
  and shared root state were backed up before the authorized uninstall/flash.
  The old app was uninstalled through Android UI and the signed release was
  installed through Android UI. Its actual installed bytes/signature match
  the delivered APK.
- The **Miuix default UI** performed the complete file-picker flow against a
  copied original image: Select a file → original init_boot copy → Next →
  `android13-5.15` → Confirm → patch/export completion. No theme change or
  command-line image patching substituted for this flow. The genuine published
  MediaStore export is retained on the phone.
- The exported 8 MiB image is saved as the new
  `kernelsu_patched_v4.1.2-modified_20261009_2307.img` in the user's `op11`
  Downloads folder, SHA256
  `5a1bd866ce638bdc2292e9f259c53627f209b9d6d704cc6d29ec2bcc93e3fad4`.
  Independent decoding confirms the release module
  `787d23547001b94d0339c19f00fb92f2e1132551e18c88ee28d10f3fd7288ab2`
  and unchanged loader. Other original CPIO contents, metadata and hardlink
  relationships are preserved. Header differences are limited to ramdisk
  length; the 832-byte AVB metadata is identical and footer offsets are correct.
- The exact exported image was flashed only to `init_boot_a`; fastboot reported
  successful send/write. Boot completed and the phone unlocked normally. The
  app reports Working LKM `40545`, Tracepoint Hook, one superuser, eight
  existing modules, and `v4.1.2 modified (40545)`.
- Production daemon installation succeeded. `/data/adb/ksud` exactly matches
  the release APK daemon, SHA256
  `50840b0e0ab82509516227811162e62d878266f583117444f5a67792eae4e880`.
  Slot B, the allowlist and shared executable contents retain their original
  hashes. Both user-original images and the original keystore remain unchanged.
- Enabling the SELinux-hide setting through the actual UI saved requested
  value `1` while runtime value stayed `0` pending reboot. After the second
  authorized reboot, runtime and saved configuration for `selinux_hide`
  (feature ID `4`) both read `1`; the UI toggle is on and displays
  **SELinux 上下文与访问查询隐藏已启用**. `su_compat` and `kernel_umount`
  remain enabled. `getenforce` still returns `Enforcing`.

The new functional release and enabled hide setting remain installed. Owned
temporary transports/scripts and staging files were removed; the genuine phone
export and host APK/image remain. The temporary Files install permission was
restored to off, the preexisting test package was left untouched, and the task
Colima VM was stopped. This round boot-tested the current `android13-5.15` phone; the other six KMIs retain
build/resource verification only. It establishes no 16 KiB page support.

Evidence remains locally in ignored `cache/release-deploy/`, including
`flash-init_boot_a.txt`, `runtime-before-enable.txt`, `runtime-before-reboot.txt`,
`runtime-final.txt`, `deployment-result.json`, the UI captures, installed APK verification,
`image-comparison.json` and `independent-image-review.json`. Genuine rollback
images, old installed APK and private state backups remain in its `rollback/`
folder. No private backup contents or signing credentials are committed.
