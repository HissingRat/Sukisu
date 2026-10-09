# SELinux query hiding backport

This branch backports the complete SELinux-hide query and initialization branches
from **tiann/KernelSU `df03912f70d92ff2aa9762ef82d607033d37e1da`** onto SukiSU
v4.1.2 (`ede8a21fb215eccca099db26214308e7f8685a47`). It compares against that pinned
source, not the latest moving main. The former ARM64-5.15-only restriction is
removed; the implementation covers SukiSU's ARM64 and x86_64 targets, with old
state-based and newer policy-based SELinux APIs and newer static-call LSM hookup.

See [the branch-by-branch parity checklist](SELINUX_HIDE_PARITY.md) for all query,
status retry, late-load, architecture, fallback and lifecycle branches, and for
the explicit safety/control-state differences. The expanded source review,
524 differential cases and twelve build rows passed. The earlier PHB110 device
test belongs to the first 5.15-only revision and does not validate this later
expansion. See the parity checklist for build coverage and its limits.

The feature uses an independently copied policy database and SID table. For
application-range UIDs it hides policy additions from context/access queries and
`setprocattr("current")` validation, and serves a captured SELinux status page.
Permission checks still use the live policy; root/system UIDs retain original
behavior. Access decision sequence numbers are normalized to 1. Early status
capture waits for an enforcing page and can retry until post-fs-data. Late-load
initialization has the upstream status-counter/enforcement normalization.

Feature ID 4 is preserved; IDs 2 and 3 remain reserved. The Manager reads actual
main-query activation separately from its saved request. Missing backup returns
EAGAIN and needs a saved setting plus reboot. As upstream, a separately available
status hook can already obey that request even while the main query group cannot
activate. A missing status hook does not prevent the other hooks from working.

For lifetime safety, callbacks stay installed as passthrough when disabled and
pin the module until reboot. Existing status descriptors keep their original
page; newly opened ones follow the switch. Published policy snapshots also remain
allocated until reboot. Normal module unload is therefore not available.

Original boot images must remain intact and any patching must use copies. The
feature source itself performs no partition flashing. Experimental image and
Manager artifacts must be correctly paired, particularly when a separate Manager
package is selected by `KSU_MANAGER_PACKAGE`.
