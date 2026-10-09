# Actual app UID SELinux validation probe

This opt-in app runs queries at its own ordinary zygote UID and SELinux domain.
It requests no Android permissions, root access, policy changes or feature writes.
The only context write attempt uses a deliberately nonexistent context in a
disposable native child. Denials are reported as observed errno values.

Build from `manager`:

```sh
./gradlew :selinuxprobe:assembleDebug -PSUKISU_BUILD_SELINUX_PROBE=true
```

Package: `com.sukisu.ultra.selinuxprobe`; activity: `.ProbeActivity`.
Output: visible selectable text, copy button, app-private `files/selinux-probe-report.txt`,
and logcat tag `SELinuxAppProbe`. Force-stop/relaunch between feature states so
status descriptors and mappings are freshly opened. Never grant the probe root
or change SELinux policy to make a denied query work.

Use a paired test module with `KSU_MANAGER_PACKAGE=com.sukisu.ultra.selinuxtest`
so this probe can never be selected as the Manager despite its development signer.
The native implementation derives from the independently reviewed standalone
probe in `cache/device-test/selinux-hide-probe.c`, with status read logging added.

The parity-round probe additionally decodes all six access fields (including
`seqno`) and queries the stock `selinuxfs` target so that a missing added `ksu_file`
type does not prevent seqno observation. Disposable child probes cover both
plain and newline-terminated invalid contexts. A class lock serializes the JNI
run and private report read across activity recreation. Previous probe APKs are
preserved; parity builds use `cache/parity-validation/*-parity-test.apk`.
