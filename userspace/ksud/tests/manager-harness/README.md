# Production Settings ViewModel regression

From `manager`:

```sh
./gradlew -p ../userspace/ksud/tests/manager-harness run
```

A Gradle Sync task copies the current production `SettingsViewModel`,
`SettingsUiState` and `SettingsRepository` interface for compilation on the JVM.
Resource messages are read from the current production English XML at runtime.
The fixture supplies Android/lifecycle/JNI/shell services and real coroutines;
it does not duplicate the ViewModel implementation.

The scenarios cover fatal enable failure with requested status-only masking,
no erroneous persistence or active switch, app relaunch with no saved request,
explicit clear despite the core getter already reporting zero, EAGAIN save and
cancel, unsupported kernels, genuine query errors, a persistence exception after
actual activation, and module-managed feature guards.

This is a state/command regression. It does not render Compose, run Android JNI,
or establish device UI correctness. Both Compose themes consume the tested
`selinuxHideCanClearRequest` property and send an explicit zero command for reset.

The Sync task also imports the actual `PatchedImageExport` helper. Controlled
Android Downloads fixtures verify exact byte export, pending publication,
cleanup after insert/open/write/publication failure, and refusal of empty images
or unsupported API calls. These fixtures do not establish actual MediaStore or
scoped-storage behavior; the device export check supplies that evidence.
