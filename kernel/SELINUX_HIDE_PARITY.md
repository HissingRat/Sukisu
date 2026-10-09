# SELinux hide parity checklist

Reference: **tiann/KernelSU `df03912f70d92ff2aa9762ef82d607033d37e1da`**,
not the moving main branch. SukiSU baseline:
`ede8a21fb215eccca099db26214308e7f8685a47` (v4.1.2).

The checklist concerns observable hidden-query behavior, including fallback and
initialization branches. It does not claim that unrelated KernelSU features or
the whole upstream kernel module were transplanted. Independent source review,
524 actual-source differential cases, and all twelve ARM64/x86_64 kernel build
rows have passed on final source inventory `e4865a515e28`. The earlier PHB110
test covered the previous 5.15-only revision, not these expanded changes.

## Query and lifecycle coverage

| Upstream branch | Backport implementation | Reference |
| --- | --- | --- |
| UID selection | Real `current_uid().val >= 10000`; system/root UIDs call the original handlers. No extra allowlist exclusions. | [upstream context](https://github.com/tiann/KernelSU/blob/df03912f70d92ff2aa9762ef82d607033d37e1da/kernel/feature/selinux_hide.c#L83) |
| Context query | Live-policy CHECK_CONTEXT permission precedes private-policy conversion; successful contexts synchronize the live SID table; canonical string returned. | [upstream context](https://github.com/tiann/KernelSU/blob/df03912f70d92ff2aa9762ef82d607033d37e1da/kernel/feature/selinux_hide.c#L93) |
| Access query | Live COMPUTE_AV permission, both contexts checked against backup, live SID synchronization, complete allowed/audit/permissive fields. Returned `avd.seqno` is always normalized to 1. | [upstream access](https://github.com/tiann/KernelSU/blob/df03912f70d92ff2aa9762ef82d607033d37e1da/kernel/feature/selinux_hide.c#L142) |
| setprocattr current | Non-current, empty and initial-newline cases pass through. Trailing newline is replaced by NUL and the shortened size is forwarded exactly as upstream. Backup-invalid contexts return the live SETCURRENT permission error before the validation error. | [upstream setprocattr](https://github.com/tiann/KernelSU/blob/df03912f70d92ff2aa9762ef82d607033d37e1da/kernel/feature/selinux_hide.c#L232) |
| Normal status capture | Early status-open hook; existing page required; non-enforcing page is not captured. Allocation failure leaves capture eligible to retry. Successful capture is immutable. | [upstream capture](https://github.com/tiann/KernelSU/blob/df03912f70d92ff2aa9762ef82d607033d37e1da/kernel/feature/selinux_hide.c#L272) |
| Delayed status capture | Second-stage attempts capture; success closes retry window, failure leaves early-open retry active. Post-fs-data closes the retry window regardless. | [upstream lifecycle](https://github.com/tiann/KernelSU/blob/df03912f70d92ff2aa9762ef82d607033d37e1da/kernel/feature/selinux_hide.c#L479) |
| Status open | Logically armed, request-enabled application UID gets the snapshot if present; otherwise original open runs, with eligible capture afterward. Already-open descriptors retain their page. | [upstream open](https://github.com/tiann/KernelSU/blob/df03912f70d92ff2aa9762ef82d607033d37e1da/kernel/feature/selinux_hide.c#L323) |
| Optional status-hook failure | Missing status operations or a failed status patch does not suppress otherwise available context/access/setprocattr hiding. Enable retries the status hook. | [upstream optional hook](https://github.com/tiann/KernelSU/blob/df03912f70d92ff2aa9762ef82d607033d37e1da/kernel/feature/selinux_hide.c#L498) |
| Missing-policy enable | Error returned without activating the main query group. A separately available status hook still follows the requested enable state, including after EAGAIN, matching upstream's status-only fallback. | [upstream enable/request](https://github.com/tiann/KernelSU/blob/df03912f70d92ff2aa9762ef82d607033d37e1da/kernel/feature/selinux_hide.c#L345) |
| Snapshot ownership | Serialize/reparse before the first SukiSU policy mutation, independent SID table initialized from initial SIDs, Android policy flags retained. No alias of the live policydb/SID table. | [upstream backup](https://github.com/tiann/KernelSU/blob/df03912f70d92ff2aa9762ef82d607033d37e1da/kernel/selinux/rules.c#L45) |
| Unused backup | When disabled at boot-completed the backup becomes logically unavailable, matching upstream EAGAIN on later enable; never-published storage is also freed. Status page has its separate lifetime. | [upstream drop](https://github.com/tiann/KernelSU/blob/df03912f70d92ff2aa9762ef82d607033d37e1da/kernel/feature/selinux_hide.c#L549) |
| Config reload | Feature-load reapplies switches; it does not recreate a missing clean policy snapshot. Existing independent backup remains unaffected by subsequent live-policy edits. | [upstream config load](https://github.com/tiann/KernelSU/blob/df03912f70d92ff2aa9762ef82d607033d37e1da/userspace/ksud/src/feature.rs#L361) |
| Late-load bootstrap | Module initialization detects non-PID-1 load with an existing policy, captures clean policy, clones/modifies/RCU-publishes a live replacement and resets AVC (creating the su domain only if missing), caches SIDs, sets the loader SELinux domain, loads allowlist/tracks manager, and avoids early-init kprobes. Built-in mode remains early-load. | [upstream bootstrap](https://github.com/tiann/KernelSU/blob/df03912f70d92ff2aa9762ef82d607033d37e1da/kernel/core/init.c#L110) |
| Late-load status | Non-enforcing capture allowed; before 6.10 normalize sequence/policyload/enforcing to 0/0/1; from 6.10 normalize to 4/1/1. Normal-load fields are preserved. | [upstream normalization](https://github.com/tiann/KernelSU/blob/df03912f70d92ff2aa9762ef82d607033d37e1da/kernel/feature/selinux_hide.c#L296) |

## Kernel and architecture branches

- The artificial ARM64-5.15-only gate is removed. Existing SukiSU's `arch.h`
  supports ARM64 and x86_64; both receive the upstream memory-patching backend.
  RISC-V is not an existing SukiSU target and needs a separate core architecture
  port; importing a RISC-V patch helper alone would not make this module support it.
- Before 6.6, the private `selinux_state` uses the existing kernel context/SID/AV
  functions. From 6.6, the upstream private-policy parser/stringifier and access
  computation helpers are included in full.
- The newer AV path resolves `context_struct_compute_av` when available, otherwise
  uses the local upstream implementation, including conditional rules, extended
  permission traversal, MLS constraints, role transition checks, type bounds and
  optional masked-permission audit. `security_dump_masked_av` remains optional.
- LSM hookup retains the upstream linked-list branch before 6.12 and static-call
  branch from 6.12. Symbol resolution retains CFI jump-table preference and
  compiler-suffixed fallback discovery. ARM64 cache-flush ABI is detected from
  the target source, covering old and new 5.10 variants. The baseline's incorrect
  remote-task seccomp flag clearing is corrected to `clear_task_syscall_work`
  on generic-entry kernels, allowing the existing x86_64 path to compile and
  clear the real SECCOMP syscall-work bit. Manual-su includes its random API
  header explicitly. The Android 12 ARM64 stack-canary workaround is restricted
  to ARM64 so it cannot conflict with x86_64's native per-CPU guard in 6.18;
  ARM64 preprocessed behavior is unchanged.
- Verified build coverage: ARM64 and x86_64 5.10, 5.15, 6.1, 6.6, 6.12 and 6.18.
  ARM64 additionally passed matching-vmlinux symbol checks. x86_64 has compile/link
  coverage without a matching vmlinux, so it does not establish module-load ABI
  compatibility.

Reference files are pinned locally under `cache/KernelSU/kernel/feature/`,
`hook/`, `infra/`, and `selinux/`. The query implementations keep upstream helper
names so the test harness can execute extracted production functions against
extracted upstream functions without rewriting the logic being compared.

## Deliberate safety and control-state adaptations

1. **No runtime callback teardown.** Installed callbacks remain as passthrough
   when disabled; module references protect callback code and status mappings.
   A policy snapshot that was once published also remains allocated until reboot.
   This avoids freeing data beneath sleeping callbacks. Logical availability is
   tracked separately: disabling before boot-completed makes later enable return
   EAGAIN exactly as upstream even though published storage remains allocated.
   A separate logical status-armed flag mirrors upstream hook visibility: a
   running-group disable and failed-install cleanup disarm it; only reaching
   the upstream status-hook setup point re-arms it. An early EAGAIN return does
   not re-arm a previously disabled status hook. This also preserves initial
   EAGAIN/fatal-early-error status-only fallback without masking extra cases.
   Normal enabled/disabled query outputs and re-enable timing are unchanged.
2. **Truthful feature getter.** `get` reports actual activation of the main query
   group; upstream reports the request bit even when installation fails. A separate
   requested bit preserves upstream's independently available status-only behavior.
   Feature ID 4 and negative errno ABI remain unchanged. Specific allocation or
   patch errors are preserved rather than all being collapsed into ENOSYS/EAGAIN.
3. **Safe publication.** Original callbacks are stored before replacement slots
   become callable. Failed main-hook installation leaves retained callbacks in
   passthrough until the entire main group is ready. Failed status installation
   retains original callback storage for any unexpected partial-fault exposure.
4. **Sleepable early setup.** Second-stage capture/rules/SID/credential setup is
   queued as PID-1 task work, preserving its order before init returns to userspace
   while avoiding sleeping allocations in a kprobe. Late-load init already has
   process context and performs the sequence directly.
5. **Local memory/error hardening.** Old-kernel canonical-context results retain
   the stock transaction-size check. The >=6.6 context helper destroys temporary
   context allocations even when SID insertion fails. Snapshot serialization is
   protected by the SELinux policy mutex and the existing SukiSU rules lock.
   A non-PID-1 load before any SELinux policy exists uses the early-init path
   rather than attempting a late bootstrap with a NULL policy.

Neither implementation hides every SELinux interface: raw-policy reads,
create/relabel/member queries, arbitrary policy/audit side channels, and generic
root/kernel-module detection are outside this feature's upstream scope. Late-load
counter normalization does not reconstruct arbitrary edits made before capture.
