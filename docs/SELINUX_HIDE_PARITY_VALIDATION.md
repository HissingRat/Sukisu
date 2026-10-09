# Full SELinux-hide parity validation

This report concerns the expanded implementation against pinned KernelSU
`df03912f70d92ff2aa9762ef82d607033d37e1da`, on SukiSU v4.1.2 baseline
`ede8a21fb215eccca099db26214308e7f8685a47`. See
[the semantic checklist](../kernel/SELINUX_HIDE_PARITY.md) for exact upstream
branches and documented safety/control-state adaptations. The earlier
[device report](SELINUX_HIDE_DEVICE_TEST.md) describes a different, narrower
revision; its artifacts and logs were retained unchanged.

## Source and build coverage

The final kernel source inventory SHA256 is
`e4865a515e28721e48970de9d4ee0931e16ff5613d73740f3c7e3e60bb67046d`.
Per-file inventories, immutable DDK image digests, commands and logs are under
`cache/parity-validation/`. The inventory hashes sorted relative source paths
and contents, excluding generated `.mod.c` files.

All twelve rows passed on this final inventory:

| Kernel branch | ARM64 compile/link + matching-vmlinux symbol check | x86_64 compile/link |
| --- | --- | --- |
| Android 12 / 5.10 | Pass | Pass |
| Android 13 / 5.15 | Pass | Pass |
| Android 14 / 6.1 | Pass | Pass |
| Android 15 / 6.6 | Pass | Pass |
| Android 16 / 6.12 | Pass | Pass |
| Android 17 / 6.18 | Pass | Pass |

The last change restricts the pre-existing ARM stack-canary workaround to ARM64,
avoiding conflict with the x86 6.18 per-CPU canary declaration. Rebuilding ARM64
5.15 after that change produced the exact same stripped module SHA256 as the
independently reviewed/flashed candidate: `b8a03162…38755cd`.

The x86 builds prepare x86 GKI headers and SELinux objects from the same DDK
source using the upstream DDK workflow. They provide compile/link coverage;
matching x86 `vmlinux` and module-version tables are unavailable, so they do not
establish x86 module load compatibility. ARM64 builds use matching DDK symbol
checks. No build result alone establishes compatibility with every OEM kernel.

Actual-source differential fixtures execute extracted production functions
against extracted pinned upstream functions across 5.15, 6.6, 6.10 and 6.12
branches, including permission/error order, access sequence normalization,
newline forwarding, status capture/retry/cutoff/late normalization and stateful
hook visibility/backup availability. Deliberate regression mutations must be
detected. Final results: 109 hook observations plus 22 lifecycle observations
per version, 524 total, with six deliberate regressions detected. The tested
`selinux_hide.c` SHA256 is
`8d2cb7764873e4cfbe5e36317abe0f24489afc0f38e882732be6ee481000afa0`.
The actual ViewModel harness passed six scenario groups, including fatal-error
relaunch/clear, pending cancellation, unsupported kernels, read and persistence
failures, and the module-operation guard. Rust state/locking/nonzero-value
regressions and isolated strict Clippy passed. Both Android daemon release
builds used `--locked`; Manager release assembly and v2-only signatures passed.
Full-project strict Clippy still reports 51 pre-existing diagnostics outside
the changed feature/CLI sources; the capped run passes with those warnings.
These fixtures supplement, rather than simulate, device evidence.

## Device preparation and artifacts

Target: PHB110, Android 15, ARM64 kernel
`5.15.167-android13-8-o-01144-gb3f32e037fab`. Before this test, root verified
original slot A, untouched slot B, original daemon/allowlist and absent feature
config/lock. Fresh shared-state and official-app-data backup archives were
copied to the host. Bootloader directly confirmed unlocked, slot A, and an
8 MiB `init_boot_a` partition. Android's spoofed locked/green properties were
not used as flashing evidence.

| Artifact | SHA256 |
| --- | --- |
| Actual original `init_boot_a` rollback image | `96868cdc024715cfd16ff2d93ab044d07713eda400e15dc49515fe787b7174f6` |
| Untouched `init_boot_b` | `c3c60246b0faa0abf46f31f37c6a2d611c2418d6d0b74f483be2357876c06b6d` |
| Test module `artifacts/android13-5.15_kernelsu-parity-test.ko` | `b8a03162bb05eb77fd8ecb5790096fa3b73e8b75b347a623f9bae065f38755cd` |
| Test image `device-run/patch/parity-init_boot_a.img` | `001c990efad51ba9e9c6b95699320956b0769ecfadde5661c47af4fbad51e898` |
| Manager `SukiSU_4.1.2-selinux-parity-test_40545-sidebyside-parity-final.apk` | `b33644692a7672afb792ee0edbcbf96c3f324a0832d198b8a9c318b543f64d03` |
| Ordinary-app probe `SELinux_App_UID_Probe-parity-test.apk` | `1eb16b4e12f900a0c0102abc544a61c3982e25f1581ff8ecd5cac2e96eff37e8` |

Relative artifact paths above are under `cache/parity-validation/`. Independent
unpacking of original and candidate found 18 ramdisk entries: only module bytes
changed, all entry metadata remained identical, boot-header changes were limited
to ramdisk length, and the existing 832-byte AVB metadata was preserved.

The Manager uses separate package `com.sukisu.ultra.selinuxtest`; the paired
module recognizes that package with the existing experimental 744-byte signing
certificate, SHA256
`7cba95aac6bbe0c34fb816789806054b70e69000edbb151fad7c2e5124c2c65d`.
It skips global daemon installation. The official package/data are retained.
Both embedded daemons were independently checked byte-for-byte against final
locked release builds. The original global daemon can apply stored raw ID 4
at boot, so replacement is unnecessary.

## Device observations and restoration

The first normal boot succeeded with root, original daemon and allowlist, and
unchanged slot B. Raw CLI value 7 was normalized to persisted value 1. Because
the unused policy backup had been dropped at boot completion, the main query
group remained inactive while the early status hook returned snapshot sequence
0 to the UID-only diagnostic process. Context/access still used the live policy.
Generic feature-save preserved the request. The Manager displayed saved/reboot
wording and its explicit clear-request action set both actual/config values to
0 and restored fresh status sequence 196. This covers upstream status-only
fallback and its cancellation, not merely successful full enable.

The saved enable request survived a normal reboot using the unchanged original
daemon: actual/config both reported 1. UID 0 retained live context/access results
and status sequence 196. A diagnostic child with UID 10340 but still in the su
SELinux domain rejected `ksu_file` context/access with EINVAL, returned stock
`selinuxfs` access `allowed=0x10, seqno=1, flags=0`, and status sequence 0. The
live control returned `allowed=0xffffffff, seqno=1, flags=1`; since its sequence
was already 1, this device cannot by itself prove normalization from larger
values (differential fixtures cover that case).

A disposable child writing its own valid context returned 9 bytes both for the
9-byte plain input and 10-byte newline input. This matched enabled and disabled
behavior; fixtures additionally inspect the exact original callback arguments.
A disposable `ksu_file` current-context write succeeded when disabled and
returned EINVAL when enabled. No parent credentials or policy were changed.

Disabling returned actual/config to 0 and fresh queries to live policy. A held
status mmap retained sequence 0 while a newly opened one returned 196. Re-enabling
with raw value 2 succeeded in the same boot and persisted canonical 1. It was
then disabled and saved as 0 for a final reboot.

The separate ordinary app ran as UID 10340 in
`u:r:untrusted_app:s0:c84,c257,c512,c768`; both before and during hiding, OEM
policy denied context/access-class/status and plain/newline current-context
operations with EACCES. This is a permission-preservation check, not proof that
an ordinary app can access and observe the alternate policy on this device.
The su-domain UID-only probes are explicitly separate diagnostic coverage.

The disabled-request reboot completed, but post-reboot unlock was unavailable.
To avoid an extra user unlock cycle, the already verified original slot A image
was restored next. Consequently this round does **not** claim an observed
actual-zero query after the disabled reboot; saved zero was verified before it,
and the earlier device round separately exercised that persistence case.
The original image booted successfully. Final root verification confirmed the
exact original A and unchanged B hashes listed above. The original daemon
(`8cbe35b8850bdf352a7c59cec4a2f36789f10069879a31ae9ebf3ff2505a1f39`),
allowlist (`48519fb4efaef232d939be5f3303e3ea108f9267d2f810b8dda1f5fa76feb59f`),
and all five shared binaries match the pre-test backup. The official APK remains
`b0d75e28ef40b5e143c598f34c8f579f7daa1dfdff9c03940ea7eb215dbbc917`.
Official Manager shows working LKM 40545, one superuser and eight modules;
SELinux is enforcing and original features 0/1 are enabled.

Task-created feature config and lock were removed, restoring original absence.
After launching the official Manager, shared-file hashes and config absence
were checked again. Both test APKs, all task scripts/probes, the UI dump and the
shared-storage export directory were removed; only the official package remains.
Host copies of logs and backup archives were verified before device cleanup. No forced module
unload, live policy mutation, deliberate
fault injection or late-load experiment is part of this device regression.
The normal-boot tests cannot establish live late-load safety or arbitrary
concurrency/stress behavior on all supported kernel versions.

## Reproduction and operational state

Run `python3 userspace/ksud/tests/kernel-parity/run.py` for the actual-source
differential checks; its README specifies the pinned reference and compiler.
Kernel build drivers are `cache/parity-validation/build_matrix.py` and
`build_x64_matrix.py`, with immutable image digests recorded in each result.
Pass all six KMI names explicitly to the ARM driver to include 5.15. The task
Colima profile can be restarted with `colima start sukisu-build`; its cached
images and `sukisu-parity-kdirs` volume are retained.

Restoration and cleanup are complete. Evidence is under
`cache/parity-validation/device-run/`: `restored.txt`,
`exports-final/final-after-official-launch.txt`, `official-restored-ui.xml`,
`cleanup-check.txt`, and `originals-after.sha256`. The task Colima VM is stopped.
Original slot A rollback image and private host backups are retained.

The user-supplied `Downloads/op11/init_boot.img` remains SHA256
`2d3b65b9ee3d5ac5684efc162111af21fe2f04c14a9e0552b8783582e3f2b8ff`;
`kernelsu_patched_20260315_134628.img` remains
`96868cdc024715cfd16ff2d93ab044d07713eda400e15dc49515fe787b7174f6`.
The backed-up official APK also retains its original hash. No original image
was modified, no bootloader unlock/wipe occurred, and slot B was not written.
