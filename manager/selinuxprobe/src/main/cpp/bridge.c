#define _GNU_SOURCE
#include <jni.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

static int report_fd = -1;
static int probe_printf(const char *format, ...) {
    char buffer[8192];
    va_list args;
    va_start(args, format);
    int count = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (count <= 0) return count;
    size_t length = (size_t)count < sizeof(buffer) ? (size_t)count : sizeof(buffer) - 1;
    size_t offset = 0;
    while (offset < length) {
        ssize_t written = write(report_fd, buffer + offset, length - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return -1;
        offset += (size_t)written;
    }
    return count;
}

#define printf probe_printf
#define main selinux_query_main
#include "probe.c"
#undef main
#undef printf

JNIEXPORT jint JNICALL Java_com_sukisu_ultra_selinuxprobe_ProbeActivity_runProbe(
        JNIEnv *env, jclass type, jstring report_path) {
    (void)type;
    const char *path = (*env)->GetStringUTFChars(env, report_path, NULL);
    if (!path) return -ENOMEM;
    report_fd = open(path, O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0600);
    int error = errno;
    (*env)->ReleaseStringUTFChars(env, report_path, path);
    if (report_fd < 0) return -error;
    // No credential-drop arguments: this stays at the app's actual UID and domain.
    char *argv[] = { "selinux-app-probe", NULL };
    int result = selinux_query_main(1, argv);
    close(report_fd);
    report_fd = -1;
    return result;
}
