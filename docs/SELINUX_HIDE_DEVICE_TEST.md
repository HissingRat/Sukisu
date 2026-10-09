# PHB110 device test, 2026-10-09

**Completed: tested the feature, then restored the original phone configuration.**
The official Manager remains installed with its data, the original slot-A image
is restored, slot B is unchanged, and temporary test apps/files were removed.

## Device and deployment

- PHB110, Android 15, kernel `5.15.167-android13-8-o-01144-gb3f32e037fab`.
- Existing Termux root authorization provided root access; no new persistent root
  grant, bootloader unlock, data wipe, or SELinux permission relaxation was used.
- Android properties reported locked/green, but raw bootconfig reported
  unlocked/orange. Actual fastboot independently confirmed `unlocked: yes`,
  `current-slot: a`, and `partition-size:init_boot_a: 0x800000` before flashing.
- Both current init_boot partitions, shared SukiSU files, and official Manager
  data were backed up to the host before testing. The actual slot-A bytes matched
  the supplied patched image; this was verified rather than assumed.

The test image was made from a copy of the actual slot-A backup using the
repository's bundled magiskboot. Two independent CPIO comparisons found exactly
18 entries, with only `kernelsu.ko` content changed; all other content and entry
metadata were identical. Header differences were limited to ramdisk size; AVB
metadata was preserved and footer offsets adjusted. The original patched image
already had a stale unsigned AVB hash descriptor; testing did not disable any
additional verification setting.

Only `init_boot_a` was flashed. The test used a separate
`com.sukisu.ultra.selinuxtest` Manager and a module built with that exact
`KSU_MANAGER_PACKAGE`, preserving the official app and avoiding ambiguous Manager
selection. The test Manager skipped automatic shared-daemon installation. The
original `/data/adb/ksud` applied feature ID 4 during normal boot without needing
replacement.

| Test artifact | SHA-256 |
| --- | --- |
| `cache/device-test/android13-5.15_kernelsu-selinuxtest.ko` | `69c5469c817ef4fa23323d5aa359764623cd2f78f17be18cdf94f275648b2b94` |
| `cache/device-test/patch/test-init_boot_a.img` | `89f881580fb9e3d96f8ad1b3e4272f9ea472badb5990d99d020f74b809278211` |
| `cache/artifacts/SukiSU_4.1.2-selinux-hide-dev_40545-sidebyside-test.apk` | `6ed1013ceeccd04e4f47e193dea8baa3a5b07a29cb8a0d1324481546203c177b` |
| `cache/artifacts/SELinux_App_UID_Probe-experimental.apk` | `1c71672efc9c243cb9f909072596ed8dc63bd3efdebf0519fb11ef8531b9f88e` |

## Results

| Check | Observed result |
| --- | --- |
| Boot/root/Manager | Test image booted normally; root worked; test Manager recognized the LKM, one existing superuser, and eight modules. Individual module functions were not exhaustively tested. |
| Enable after boot completion | Actual state stayed 0; Manager showed the saved request and reboot requirement, with the switch still off. |
| Cancel pending enable | Manager cancellation removed the pending state; kernel value and saved value were both 0. |
| Unrelated feature save | Generic feature save preserved the pending saved value 1 while actual value remained 0. |
| Enable persistence | Normal reboot with the original daemon produced actual value 1 and saved value 1. Manager showed the checked switch and active summary. |
| Root UID 0 control | Context/access queries retained live-policy results; status sequence remained 196 in both states. |
| Diagnostic UID 10330, `su` domain | Disabled: `ksu_file` context/access succeeded. Enabled: both returned `EINVAL`; existing `su` context remained valid. |
| Status mapping | Enabled diagnostic process received snapshot sequence 0. After disabling, its existing mmap stayed at 0 while a fresh descriptor returned live sequence 196. |
| `setprocattr("current")` | Disposable diagnostic child setting `u:object_r:ksu_file:s0` succeeded while disabled and returned `EINVAL` while enabled. Parent credentials stayed unchanged. |
| Disable and persistence | Disabling immediately restored live diagnostic results, saved value 0, and remained actual/saved 0 after another normal reboot. |
| Actual ordinary app | Zygote-created UID 10330 in `untrusted_app` received `EACCES` on context/access/status/setprocattr with hiding off, on, and after original-image restoration. |

The UID-only diagnostic deliberately retained the privileged `su` SELinux domain
to exercise the UID filter without changing policy. It is **not** evidence that
ordinary apps can access these query surfaces. This ROM denied the ordinary app
before the hiding hooks could affect those results. All diagnostic UID/domain
changes were confined to temporary processes; no persistent app permissions or
SELinux policy were changed.

The first standalone static probe failed in the Android loader before running
because of TLS alignment. It was replaced with an NDK-built dynamic PIE probe;
all reported standalone results above came from that corrected executable.
Long-running concurrency/fault-injection testing and other device/kernel variants
were not tested.

## Restoration and cleanup

After testing, the original image was restored with an explicit
`fastboot flash init_boot_a <actual-slot-A-backup>` and normal reboot. Root then
read the block devices and shared files again:

| Restored/unchanged item | Verified SHA-256 |
| --- | --- |
| Actual `init_boot_a` | `96868cdc024715cfd16ff2d93ab044d07713eda400e15dc49515fe787b7174f6` |
| Actual untouched `init_boot_b` | `c3c60246b0faa0abf46f31f37c6a2d611c2418d6d0b74f483be2357876c06b6d` |
| Original `/data/adb/ksud` | `8cbe35b8850bdf352a7c59cec4a2f36789f10069879a31ae9ebf3ff2505a1f39` |
| Original allowlist | `48519fb4efaef232d939be5f3303e3ea108f9267d2f810b8dda1f5fa76feb59f` |
| Installed official Manager APK | `b0d75e28ef40b5e143c598f34c8f579f7daa1dfdff9c03940ea7eb215dbbc917` |

All five shared binary-asset hashes matched their backups. The feature config
and lock did not originally exist; their absence was restored and verified.
The original daemon version `4.1.2-2-gf39c001e`, enabled su-compat/kernel-umount,
root access, enforcing SELinux, and official Manager's one-superuser/eight-module
view were confirmed. The temporary system-app display preference was returned
to its original off state.

Both test APKs were uninstalled. Task-created scripts, native probes, UI dumps,
and `/sdcard/Download/SukiSU-device-test-20261009` exports were removed after host
copies were verified. Only the original `com.sukisu.ultra` package remains.
The two original images in `/Users/andy/Downloads/op11` retained the hashes
recorded in [build validation](SELINUX_HIDE_VALIDATION.md); the backed-up official
APK retained the installed-APK hash listed above.
The task-created Colima VM was stopped.

Evidence is retained locally in ignored `cache/device-test/`, especially
`test-kernel-enabled.txt`, `disable-watch.txt`, `setcurrent-differential.txt`,
`disabled-after-reboot.txt`, `original-restored-root.txt`, and
`cleanup-check.txt`. Host root/app-data backup archives remain local and are not
committed. The phone is running its original image, not the experimental build.
