#!/bin/sh
# Stage verified inputs, compile the daemon, and build/sign an ARM64-only test APK.
# Arguments are the --module/--loader/hash/provenance options documented for stage.py.
set -eu
cd "$(dirname "$0")/../.."
: "${ANDROID_HOME:?Set ANDROID_HOME}"
: "${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME}"
: "${JAVA_HOME:?Set JAVA_HOME to Java 21}"
: "${INSTALLER_KEYSTORE:?Set INSTALLER_KEYSTORE to the paired development keystore}"
: "${INSTALLER_KEY_ALIAS:?Set INSTALLER_KEY_ALIAS}"
: "${INSTALLER_STORE_PASSWORD:?Set INSTALLER_STORE_PASSWORD}"
: "${INSTALLER_KEY_PASSWORD:?Set INSTALLER_KEY_PASSWORD}"
export SUKISU_VERSION_CODE=40545
profile=standalone-patch
export SUKISU_VERSION_NAME=4.1.2-selinux-installer-test
for argument in "$@"; do
    case "$argument" in
        --module-set|--module-set=*)
            profile=multi-kmi
            export SUKISU_VERSION_NAME=4.1.2-selinux-installer-multi-kmi-test
            ;;
    esac
done
output="cache/$profile"
export CARGO_TARGET_DIR="$PWD/cache/standalone-patch/ksud-target"
mkdir -p "$output"
python3 scripts/installer/stage.py "$@" --manifest "$output/staged-resources.json"
(cd userspace/ksud && cargo ndk -t arm64-v8a -P 26 build --release --locked)
cp "$CARGO_TARGET_DIR/aarch64-linux-android/release/ksud" manager/app/src/main/jniLibs/arm64-v8a/libksud.so
(cd manager && ./gradlew :app:assembleRelease \
    -PSUKISU_APPLICATION_ID=com.sukisu.ultra.selinuxtest \
    -PSUKISU_APP_LABEL='SukiSU SELinux Installer Test' \
    -PSUKISU_SKIP_DAEMON_INSTALL=true -PSUKISU_INSTALLER_4K_ONLY=true -PSUKISU_ABIS=arm64-v8a \
    -PSUKISU_VERSION_CODE="$SUKISU_VERSION_CODE" -PSUKISU_VERSION_NAME="$SUKISU_VERSION_NAME")
tools="$ANDROID_HOME/build-tools/36.1.0"
unsigned="manager/app/build/outputs/apk/release/SukiSU_${SUKISU_VERSION_NAME}_${SUKISU_VERSION_CODE}-release.apk"
aligned="$output/installer-aligned.apk"
final="$output/SukiSU_${SUKISU_VERSION_NAME}_${SUKISU_VERSION_CODE}-arm64-final.apk"
"$tools/zipalign" -f -P 16 4 "$unsigned" "$aligned"
"$tools/apksigner" sign --ks "$INSTALLER_KEYSTORE" --ks-key-alias "$INSTALLER_KEY_ALIAS" \
    --ks-pass env:INSTALLER_STORE_PASSWORD --key-pass env:INSTALLER_KEY_PASSWORD \
    --v1-signing-enabled false --v2-signing-enabled true --v3-signing-enabled false --v4-signing-enabled false \
    --out "$final" "$aligned"
"$tools/apksigner" verify --verbose --print-certs "$final" > "$output/apk-signature.txt"
cat "$output/apk-signature.txt"
python3 scripts/installer/verify-apk.py "$final" --daemon "$CARGO_TARGET_DIR/aarch64-linux-android/release/ksud" \
    --resources "$output/staged-resources.json" --signature "$output/apk-signature.txt"
echo "$final"
