# SELinux hook parity regression

Run from the repository root:

```sh
python3 userspace/ksud/tests/kernel-parity/run.py
```

The reference checkout defaults to `cache/KernelSU`; use `--upstream PATH` to
select another checkout. It must be exactly commit
`df03912f70d92ff2aa9762ef82d607033d37e1da`, and its SELinux source must equal the
Git object. The runner extracts named **actual function definitions** from that
source and the current `kernel/selinux_hide.c`, compiles both, executes the same
scenario traces, and compares their observable return values, transaction
responses, forwarded `setprocattr` bytes/lengths and selected status pages.
Missing or ambiguous definitions and changed mutation anchors fail explicitly.
No copied implementation is stored in the fixture.

The matrix selects the real preprocessor branches for 5.15, 6.6, 6.10 and 6.12.
It covers context/access errors and permissions, access seqno normalization,
newline/empty/other-attribute `setprocattr`, system UID passthrough, early missing
or permissive status pages, allocation/open failures and retry, second-stage and
post-fs cutoff, immutable snapshots, late-load normalization and noncanonical
nonzero enforcement preservation.

Stateful controller traces execute actual enable/set/get/boot-completed bodies:
raw nonzero values, initial EAGAIN -> clear -> repeated EAGAIN -> snapshot input
-> enable/disable, active versus unused boot completion, re-enable after prior
disable, early fatal errors, errors after reaching status installation, and
optional status-hook absence. The safe backport's actual-state getter and
preserved patch errno are checked as explicit intentional differences from
upstream's requested-state getter and collapsed ENOSYS error.

Six deliberate mutations must produce observable differences, proving the
fixtures detect the earlier seqno, newline, status normalization, logical arming
and backup lifetime omissions rather than accepting two arbitrary equal outputs.

Kernel permissions, SID conversion, AV computation, symbol resolution, patching,
allocation and locking are controlled service stubs. Therefore these tests
validate the executed hook/controller branches, **not** real policy algorithms,
ABI layouts, CFI, RCU/concurrency safety or device boot timing. In particular,
6.6+ AV internals use a service stub here; the full kernel implementation still
needs its own build and runtime validation on those kernels.
