/* Read-only SELinux query probe. Optional credential drop affects this process only. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int read_text(const char *path, char *buf, size_t cap)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, cap - 1);
    int error = errno;
    close(fd);
    errno = error;
    if (n < 0) return -1;
    buf[n] = 0;
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\0')) buf[--n] = 0;
    return 0;
}

static void transaction(const char *name, const char *payload)
{
    char path[128], response[4096];
    snprintf(path, sizeof(path), "/sys/fs/selinux/%s", name);
    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        printf("%s input=%s open_errno=%d (%s)\n", name, payload, errno, strerror(errno));
        return;
    }
    ssize_t n = write(fd, payload, strlen(payload));
    if (n < 0) {
        printf("%s input=%s write_errno=%d (%s)\n", name, payload, errno, strerror(errno));
        close(fd);
        return;
    }
    n = read(fd, response, sizeof(response) - 1);
    if (n < 0) printf("%s input=%s read_errno=%d (%s)\n", name, payload, errno, strerror(errno));
    else {
        response[n] = 0;
        printf("%s input=%s result=%s\n", name, payload, response);
        if (!strcmp(name, "access")) {
            unsigned allowed, decided, auditallow, auditdeny, seqno, flags;
            if (sscanf(response, "%x %x %x %x %u %x", &allowed, &decided,
                    &auditallow, &auditdeny, &seqno, &flags) == 6)
                printf("access decoded allowed=%x decided=%x auditallow=%x auditdeny=%x seqno=%u flags=%x\n",
                    allowed, decided, auditallow, auditdeny, seqno, flags);
            else printf("access response_parse_failed\n");
        }
    }
    close(fd);
}

static void status_page(void)
{
    int fd = open("/sys/fs/selinux/status", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        printf("status open_errno=%d (%s)\n", errno, strerror(errno));
        return;
    }
    uint32_t raw[5] = {0};
    ssize_t bytes = pread(fd, raw, sizeof(raw), 0);
    if (bytes < 0) printf("status read_errno=%d (%s)\n", errno, strerror(errno));
    else if (bytes != sizeof(raw)) printf("status read_bytes=%zd (expected %zu)\n", bytes, sizeof(raw));
    else printf("status read version=%u sequence=%u enforcing=%u policyload=%u deny_unknown=%u\n",
        raw[0], raw[1], raw[2], raw[3], raw[4]);
    size_t size = (size_t)sysconf(_SC_PAGESIZE);
    volatile uint32_t *map = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) printf("status mmap_errno=%d (%s)\n", errno, strerror(errno));
    else {
        uint32_t fields[5], after;
        unsigned tries;
        for (tries = 0; tries < 10000; tries++) {
            fields[1] = map[1];
            if (fields[1] & 1) continue;
            __atomic_thread_fence(__ATOMIC_ACQUIRE);
            fields[0] = map[0]; fields[2] = map[2]; fields[3] = map[3]; fields[4] = map[4];
            __atomic_thread_fence(__ATOMIC_ACQUIRE);
            after = map[1];
            if (after == fields[1]) break;
        }
        if (tries == 10000) printf("status unstable\n");
        else printf("status version=%u sequence=%u enforcing=%u policyload=%u deny_unknown=%u\n",
                    fields[0], fields[1], fields[2], fields[3], fields[4]);
        munmap((void *)map, size);
    }
    close(fd);
}

static int change_context(const char *context)
{
    int fd = open("/proc/self/attr/current", O_WRONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t n = write(fd, context, strlen(context));
    int error = errno;
    close(fd);
    errno = error;
    return n == (ssize_t)strlen(context) ? 0 : -1;
}

int main(int argc, char **argv)
{
    /* Usage: probe [numeric_uid [existing_app_context]]
     * No UID argument = control query at the caller's existing credentials.
     * UID only = UID branch test, NOT an untrusted-app-domain test.
     */
    char context[1024], class_text[64], query[2048];
    int file_class = 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 3) return 2;
    if (!read_text("/sys/fs/selinux/class/file/index", class_text, sizeof(class_text)))
        file_class = atoi(class_text);
    else printf("access file_class_read_errno=%d (%s)\n", errno, strerror(errno));
    if (argc >= 2) {
        char *end = NULL;
        errno = 0;
        unsigned long id = strtoul(argv[1], &end, 10);
        if (errno || !*argv[1] || *end || id > UINT32_MAX || id < 10000) {
            fprintf(stderr, "expected an app UID >= 10000\n");
            return 2;
        }
        if (setgroups(0, NULL) || setresgid((gid_t)id, (gid_t)id, (gid_t)id) ||
            setresuid((uid_t)id, (uid_t)id, (uid_t)id)) {
            printf("credential_drop errno=%d (%s)\n", errno, strerror(errno));
            return 3;
        }
        if (argc == 3 && change_context(argv[2])) {
            printf("context_transition errno=%d (%s)\n", errno, strerror(errno));
            return 4;
        }
    }
    if (read_text("/proc/self/attr/current", context, sizeof(context))) {
        printf("identity uid=%u euid=%u context_errno=%d\n", getuid(), geteuid(), errno);
        return 5;
    }
    printf("identity uid=%u euid=%u gid=%u context=%s\n", getuid(), geteuid(), getgid(), context);
    if (argc == 3 && strcmp(context, argv[2])) {
        printf("context_transition mismatch; refusing to mislabel this as an app-domain test\n");
        return 6;
    }
    transaction("context", context);
    transaction("context", "u:object_r:ksu_file:s0");
    transaction("context", "u:r:ksu_selinux_hide_probe_nonexistent:s0");
    if (file_class > 0) {
        snprintf(query, sizeof(query), "%s u:object_r:ksu_file:s0 %d", context, file_class);
        transaction("access", query);
        // A stock target is needed to observe the access response seqno even when
        // the added ksu_file type is correctly rejected by the snapshot.
        snprintf(query, sizeof(query), "%s u:object_r:selinuxfs:s0 %d", context, file_class);
        transaction("access", query);
    } else printf("access file_class unavailable\n");
    status_page();
    /* ART is multithreaded: the child uses only async-signal-safe syscalls,
     * reports fixed binary data through a pipe, and never calls JNI/stdio. */
    for (unsigned newline = 0; newline < 2; newline++) {
    int channels[2];
    if (pipe2(channels, O_CLOEXEC) < 0) {
        printf("setprocattr pipe_errno=%d (%s)\n", errno, strerror(errno));
        return 0;
    }
    pid_t child = fork();
    if (!child) {
        close(channels[0]);
        const char *invalid = newline ? "u:r:ksu_selinux_hide_probe_nonexistent:s0\n" :
            "u:r:ksu_selinux_hide_probe_nonexistent:s0";
        const size_t length = newline ? sizeof("u:r:ksu_selinux_hide_probe_nonexistent:s0\n") - 1 :
            sizeof("u:r:ksu_selinux_hide_probe_nonexistent:s0") - 1;
        int result[2] = {-1, 0};
        int attr = open("/proc/self/attr/current", O_WRONLY | O_CLOEXEC);
        if (attr < 0) result[1] = errno;
        else {
            ssize_t bytes = write(attr, invalid, length);
            result[0] = bytes == (ssize_t)(length) ? 0 : -1;
            result[1] = result[0] ? errno : 0;
            close(attr);
        }
        (void)write(channels[1], result, sizeof(result));
        close(channels[1]);
        _exit(result[0] ? 0 : 7);
    }
    int fork_error = errno;
    close(channels[1]);
    if (child < 0) printf("setprocattr newline=%u fork_errno=%d (%s)\n", newline, fork_error, strerror(fork_error));
    else {
        int result[2], status;
        ssize_t bytes;
        do { bytes = read(channels[0], result, sizeof(result)); } while (bytes < 0 && errno == EINTR);
        if (bytes == sizeof(result)) printf("setprocattr disposable_child newline=%u ret=%d errno=%d (%s)\n",
            newline, result[0], result[1], strerror(result[1]));
        else printf("setprocattr newline=%u child_report_bytes=%zd errno=%d\n", newline, bytes, bytes < 0 ? errno : 0);
        if (waitpid(child, &status, 0) < 0) printf("setprocattr wait_errno=%d\n", errno);
        else printf("setprocattr newline=%u child_wait_status=%d\n", newline, status);
    }
    close(channels[0]);
    }
    return 0;
}
