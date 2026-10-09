#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
mkdir -p cache/standalone-patch/loader
: "${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME to Android NDK 29.0.14206865}"
export CARGO_NET_GIT_FETCH_WITH_CLI=true
export GIT_CONFIG_COUNT=1
export GIT_CONFIG_KEY_0=url.https://github.com/KernelSU2/rustix.git.insteadOf
export GIT_CONFIG_VALUE_0=https://github.com/Kernel-SU/rustix.git
export CARGO_TARGET_AARCH64_UNKNOWN_LINUX_MUSL_LINKER="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/${NDK_HOST_TAG:-darwin-x86_64}/bin/aarch64-linux-android26-clang"
export RUSTFLAGS="-C link-arg=-no-pie"
export CARGO_TARGET_DIR="$PWD/cache/standalone-patch/loader/target"
cargo build --manifest-path userspace/ksuinit/Cargo.toml --target aarch64-unknown-linux-musl --release --locked
cp "$CARGO_TARGET_DIR/aarch64-unknown-linux-musl/release/ksuinit" cache/standalone-patch/loader/ksuinit-aarch64
