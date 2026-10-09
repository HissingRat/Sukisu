// SPDX-License-Identifier: GPL-2.0-only
/* SELinux hide query semantics from KernelSU df03912f70d92ff2aa9762ef82d607033d37e1da.
 * Permanent passthrough callbacks and module references protect sleeping callers
 * and existing status mappings. See SELINUX_HIDE_PARITY.md for adaptation details.
 */
#include "selinux_hide.h"
#include "infra/symbol_resolver.h"
#include "linux/jump_label.h"
#include "selinux/sepolicy.h"
#include <linux/cred.h>
#include <linux/cpu.h>
#include <linux/memory.h>
#include <linux/uaccess.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <asm-generic/errno-base.h>
#include <net/genetlink.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
// security/selinux/include/security.h
#include <security.h>
#include <ss/context.h>
#include <ss/services.h>
#include <ss/mls.h>
#include <ss/conditional.h>
#include "avc.h"
#include "klog.h" // IWYU pragma: keep
#include "linux/kallsyms.h"
#include "objsec.h"
#include "hook/patch_memory.h"
#include "ksu.h"
#include "feature.h"
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/lockdep.h>
#include "hook/lsm_hook.h"

static DEFINE_MUTEX(selinux_hide_mutex);
static bool ksu_selinux_hide_enabled __read_mostly = false;
static bool ksu_selinux_hide_running __read_mostly = false;
/* Requested state drives the upstream status-only fallback after an enable error. */
static bool requested_enabled;
/* Logical presence mirrors upstream unhook/re-arm without unsafe physical removal. */
static bool status_armed;
static bool module_pinned;
static bool backup_attempted;
static bool backup_usable;
static bool context_installed, access_installed, status_installed;
static int backup_error = -EAGAIN;
static struct selinux_policy *backup_sepolicy;

static void pin_callbacks(void)
{
    if (module_pinned)
        return;
    /* This also runs during module init, where a LIVE-only reference is unsuitable. */
    if (THIS_MODULE)
        __module_get(THIS_MODULE);
    module_pinned = true;
}

enum sel_inos {
    SEL_ROOT_INO = 2,
    SEL_LOAD, /* load policy */
    SEL_ENFORCE, /* get or set enforcing status */
    SEL_CONTEXT, /* validate context */
    SEL_ACCESS, /* compute access decision */
    SEL_CREATE, /* compute create labeling decision */
    SEL_RELABEL, /* compute relabeling decision */
    SEL_USER, /* compute reachable user contexts */
    SEL_POLICYVERS, /* return policy version for this kernel */
    SEL_COMMIT_BOOLS, /* commit new boolean values */
    SEL_MLS, /* return if MLS policy is enabled */
    SEL_DISABLE, /* disable SELinux until next reboot */
    SEL_MEMBER, /* compute polyinstantiation membership decision */
    SEL_CHECKREQPROT, /* check requested protection, not kernel-applied one */
    SEL_COMPAT_NET, /* whether to use old compat network packet controls */
    SEL_REJECT_UNKNOWN, /* export unknown reject handling to userspace */
    SEL_DENY_UNKNOWN, /* export unknown deny handling to userspace */
    SEL_STATUS, /* export current status using mmap() */
    SEL_POLICY, /* allow userspace to read the in kernel policy */
    SEL_VALIDATE_TRANS, /* compute validatetrans decision */
    SEL_INO_NEXT, /* The next inode number to use */
};

typedef ssize_t (*write_op_fn)(struct file *, char *, size_t);

static write_op_fn *selinux_write_op;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
static int security_context_to_sid_with_policy(struct selinux_policy *policy, const char *scontext, u32 scontext_len,
                                               u32 *sid, u32 def_sid, gfp_t gfp_flags);
static int security_sid_to_context_with_policy(struct selinux_policy *policy, u32 sid, char **scontext,
                                               u32 *scontext_len);
static void security_compute_av_user_with_policy(struct selinux_policy *policy, u32 ssid, u32 tsid, u16 tclass,
                                                 struct av_decision *avd);
static void (*security_dump_masked_av_fn)(struct policydb *policydb, struct context *scontext, struct context *tcontext,
                                          u16 tclass, u32 permissions, const char *reason) = NULL;
static void (*context_struct_compute_av_fn)(struct policydb *policydb, struct context *scontext,
                                            struct context *tcontext, u16 tclass, struct av_decision *avd,
                                            struct extended_perms *xperms) = NULL;
#else
static struct selinux_state fake_state;
#endif

static write_op_fn *context_write, *access_write;
static write_op_fn orig_context_write, orig_access_write;

static ssize_t my_write_context(struct file *file, char *buf, size_t size)
{
    // apply to all app uids
    if (likely(current_uid().val < 10000 || !smp_load_acquire(&ksu_selinux_hide_running))) {
        return orig_context_write(file, buf, size);
    }
    char *canon = NULL;
    u32 sid, len, tmp;
    ssize_t length;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
    length = avc_has_perm(current_sid(), SECINITSID_SECURITY, SECCLASS_SECURITY, SECURITY__CHECK_CONTEXT, NULL);
    if (length)
        goto out;
    length = security_context_to_sid_with_policy(backup_sepolicy, buf, size, &sid, SECSID_NULL, GFP_KERNEL);
    if (length) {
        goto out;
    } else {
        // sync to global sidtab
        security_context_to_sid(buf, size, &tmp, GFP_KERNEL);
    }

    length = security_sid_to_context_with_policy(backup_sepolicy, sid, &canon, &len);
    if (length)
        goto out;

    length = -ERANGE;
    if (len > SIMPLE_TRANSACTION_LIMIT) {
        pr_err("SELinux: %s:  context size (%u) exceeds "
               "payload max\n",
               __func__, len);
        goto out;
    }
#else
    length = avc_has_perm(&selinux_state, current_sid(), SECINITSID_SECURITY, SECCLASS_SECURITY,
                          SECURITY__CHECK_CONTEXT, NULL);
    if (length)
        goto out;

    length = security_context_to_sid(&fake_state, buf, size, &sid, GFP_KERNEL);
    if (length) {
        goto out;
    } else {
        // sync to global sidtab
        security_context_to_sid(&selinux_state, buf, size, &tmp, GFP_KERNEL);
    }

    length = security_sid_to_context(&fake_state, sid, &canon, &len);
    if (length)
        goto out;
#endif

    if (len > SIMPLE_TRANSACTION_LIMIT) {
        length = -ERANGE;
        goto out;
    }
    memcpy(buf, canon, len);
    length = len;
out:
    kfree(canon);
    return length;
}

static ssize_t my_write_access(struct file *file, char *buf, size_t size)
{
    // apply to all app uids
    if (likely(current_uid().val < 10000 || !smp_load_acquire(&ksu_selinux_hide_running))) {
        return orig_access_write(file, buf, size);
    }
    char *scon = NULL, *tcon = NULL;
    u32 ssid, tsid, sconlen, tconlen, tmp;
    u16 tclass;
    struct av_decision avd;
    ssize_t length;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
    length = avc_has_perm(current_sid(), SECINITSID_SECURITY, SECCLASS_SECURITY, SECURITY__COMPUTE_AV, NULL);
#else
    length =
        avc_has_perm(&selinux_state, current_sid(), SECINITSID_SECURITY, SECCLASS_SECURITY, SECURITY__COMPUTE_AV, NULL);
#endif
    if (length)
        goto out;

    length = -ENOMEM;
    scon = kzalloc(size + 1, GFP_KERNEL);
    if (!scon)
        goto out;

    length = -ENOMEM;
    tcon = kzalloc(size + 1, GFP_KERNEL);
    if (!tcon)
        goto out;

    length = -EINVAL;
    if (sscanf(buf, "%s %s %hu", scon, tcon, &tclass) != 3)
        goto out;

    sconlen = strlen(scon);
    tconlen = strlen(tcon);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
    length = security_context_to_sid_with_policy(backup_sepolicy, scon, sconlen, &ssid, SECSID_NULL, GFP_KERNEL);
    if (length) {
        goto out;
    } else {
        // sync to global sidtab
        security_context_to_sid(scon, sconlen, &tmp, GFP_KERNEL);
    }

    length = security_context_to_sid_with_policy(backup_sepolicy, tcon, tconlen, &tsid, SECSID_NULL, GFP_KERNEL);
    if (length) {
        goto out;
    } else {
        // sync to global sidtab
        security_context_to_sid(tcon, tconlen, &tmp, GFP_KERNEL);
    }

    security_compute_av_user_with_policy(backup_sepolicy, ssid, tsid, tclass, &avd);
#else
    length = security_context_to_sid(&fake_state, scon, sconlen, &ssid, GFP_KERNEL);
    if (length) {
        goto out;
    } else {
        // sync to global sidtab
        security_context_to_sid(&selinux_state, scon, sconlen, &tmp, GFP_KERNEL);
    }

    length = security_context_to_sid(&fake_state, tcon, tconlen, &tsid, GFP_KERNEL);
    if (length) {
        goto out;
    } else {
        // sync to global sidtab
        security_context_to_sid(&selinux_state, tcon, tconlen, &tmp, GFP_KERNEL);
    }

    security_compute_av_user(&fake_state, ssid, tsid, tclass, &avd);
#endif

    // stock reads 1; a loader load_policy may have bumped the backup before we load
    avd.seqno = 1;
    length = scnprintf(buf, SIMPLE_TRANSACTION_LIMIT, "%x %x %x %x %u %x", avd.allowed, 0xffffffff, avd.auditallow,
                       avd.auditdeny, avd.seqno, avd.flags);
out:
    kfree(tcon);
    kfree(scon);
    return length;
}

static int my_setprocattr(const char *name, void *value, size_t size);
struct ksu_lsm_hook selinux_setprocattr_hook = KSU_LSM_HOOK_INIT(setprocattr, "selinux_setprocattr", my_setprocattr, 0);

typedef int (*setprocattr_fn)(const char *name, void *value, size_t size);
static int __nocfi my_setprocattr(const char *name, void *value, size_t size)
{
    int error, perm_error;
    u32 mysid, sid;
    char *str = value;
    if (likely(current_uid().val < 10000 || !smp_load_acquire(&ksu_selinux_hide_running))) {
        goto call_orig;
    }

    if (strcmp(name, "current")) {
        goto call_orig;
    }
    if (size && str[0] && str[0] != '\n') {
        if (str[size - 1] == '\n') {
            str[size - 1] = 0;
            size--;
        }
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
        error = security_context_to_sid_with_policy(backup_sepolicy, str, size, &sid, SECSID_NULL, GFP_KERNEL);
#else
        error = security_context_to_sid(&fake_state, str, size, &sid, GFP_KERNEL);
#endif
        if (error) {
            mysid = current_sid();
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
            perm_error = avc_has_perm(mysid, mysid, SECCLASS_PROCESS, PROCESS__SETCURRENT, NULL);
#else
            perm_error = avc_has_perm(&selinux_state, mysid, mysid, SECCLASS_PROCESS, PROCESS__SETCURRENT, NULL);
#endif
            return perm_error ?: error;
        }
    }

call_orig:
    return ((setprocattr_fn)selinux_setprocattr_hook.original)(name, value, size);
}

static DEFINE_STATIC_KEY_FALSE(fake_status_initialize_key);
static struct page *fake_status = NULL;

static bool status_capture_allowed(bool page_present, bool enforcing, bool late_loaded)
{
    return page_present && (enforcing || late_loaded);
}

static void normalize_status_snapshot(struct selinux_kernel_status *status, bool late_loaded)
{
    if (!late_loaded)
        return;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 10, 0)
    status->sequence = 4;
    status->policyload = 1;
#else
    status->sequence = 0;
    status->policyload = 0;
#endif
    if (!status->enforcing)
        status->enforcing = 1;
}

static void initialize_fake_status()
{
    mutex_lock(&selinux_state.status_lock);
    if (fake_status)
        goto out;
    if (!selinux_state.status_page) {
        pr_warn("initialize_fake_status: status_page not exist\n");
        goto out;
    }

    struct selinux_kernel_status *status = page_address(selinux_state.status_page);
    if (!status_capture_allowed(true, status->enforcing, ksu_late_loaded)) {
        pr_warn("initialize_fake_status: skip not enforcing\n");
        goto out;
    }

    struct page *new_page = alloc_page(GFP_KERNEL | __GFP_ZERO);
    if (!new_page) {
        pr_err("initialize_fake_status: failed to allocate page\n");
        goto out;
    }

    struct selinux_kernel_status *new_status = page_address(new_page);
    memcpy(new_status, status, sizeof(*status));
    normalize_status_snapshot(new_status, ksu_late_loaded);

    fake_status = new_page;
    pr_info("initialize_fake_status initialized: sequence=%d, policyload=%d, enforcing=%d\n", new_status->sequence,
            new_status->policyload, new_status->enforcing);

out:
    mutex_unlock(&selinux_state.status_lock);
}

typedef int (*sel_open_handle_status_fn)(struct inode *inode, struct file *filp);
static sel_open_handle_status_fn orig_sel_open_handle_status, *sel_open_handle_status_slot;
static int my_sel_open_handle_status(struct inode *inode, struct file *filp)
{
    if (likely(current_uid().val >= 10000 && READ_ONCE(status_armed) && READ_ONCE(requested_enabled))) {
        void *data;
        mutex_lock(&selinux_state.status_lock);
        data = fake_status;
        mutex_unlock(&selinux_state.status_lock);
        if (data) {
            filp->private_data = data;
            return 0;
        }
    }

    int ret = orig_sel_open_handle_status(inode, filp);
    if (READ_ONCE(status_armed) && static_branch_unlikely(&fake_status_initialize_key) && !ret && !fake_status) {
        initialize_fake_status();
    }
    return ret;
}


static bool policy_exposed;

/* Status hiding is deliberately optional, matching the upstream fallback. */
static void hook_selinux_status_open(void)
{
    struct file_operations *ops;
    sel_open_handle_status_fn replacement = my_sel_open_handle_status;
    int ret;

    if (status_installed) {
        WRITE_ONCE(status_armed, true);
        return;
    }
    ops = (void *)find_kernel_symbol_exact("sel_handle_status_ops");
    if (!ops || !ops->open) {
        pr_warn("selinux_hide: status hook unavailable; other query hooks remain usable\n");
        return;
    }
    sel_open_handle_status_slot = &ops->open;
    if (!orig_sel_open_handle_status)
        orig_sel_open_handle_status = READ_ONCE(ops->open);
    if (READ_ONCE(ops->open) == replacement) {
        status_installed = true;
        WRITE_ONCE(status_armed, true);
        return;
    }
    pin_callbacks();
    ret = ksu_patch_text(sel_open_handle_status_slot, &replacement, sizeof(replacement),
                         KSU_PATCH_TEXT_FLUSH_DCACHE);
    if (ret) {
        pr_err("selinux_hide: status hook patch failed: %d\n", ret);
        /* An aligned pointer patch cannot cross a page. Preserve the original
         * callback even on an unexpected partial fault and retain module lifetime. */
    } else {
        status_installed = true;
        WRITE_ONCE(status_armed, true);
    }
}

static int ksu_selinux_hide_enable(void)
{
    write_op_fn replacement;
    int ret;

    if (!backup_sepolicy || !backup_usable)
        return backup_error ?: -EAGAIN;
    selinux_write_op = (void *)find_kernel_symbol_exact("write_op");
    if (!selinux_write_op)
        return -ENOSYS;
    hook_selinux_status_open();
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
    security_dump_masked_av_fn = (void *)find_kernel_symbol_exact("security_dump_masked_av");
    context_struct_compute_av_fn = (void *)find_kernel_symbol_exact("context_struct_compute_av");
    /* Missing compute helper uses the complete upstream local AV implementation. */
#else
    fake_state.initialized = true;
    fake_state.policy = backup_sepolicy;
#endif
    context_write = &selinux_write_op[SEL_CONTEXT];
    access_write = &selinux_write_op[SEL_ACCESS];
    if (!*context_write || !*access_write) {
        ret = -ENOSYS;
        goto unhook;
    }
    pin_callbacks();
    if (!context_installed) {
        if (!orig_context_write)
            orig_context_write = READ_ONCE(*context_write);
        replacement = my_write_context;
        if (READ_ONCE(*context_write) != replacement) {
            ret = ksu_patch_text(context_write, &replacement, sizeof(replacement), KSU_PATCH_TEXT_FLUSH_DCACHE);
            if (ret)
                goto unhook;
        }
        context_installed = true;
    }
    if (!access_installed) {
        if (!orig_access_write)
            orig_access_write = READ_ONCE(*access_write);
        replacement = my_write_access;
        if (READ_ONCE(*access_write) != replacement) {
            ret = ksu_patch_text(access_write, &replacement, sizeof(replacement), KSU_PATCH_TEXT_FLUSH_DCACHE);
            if (ret)
                goto unhook;
        }
        access_installed = true;
    }
    if (!selinux_setprocattr_hook.entry) {
        ret = ksu_lsm_hook(&selinux_setprocattr_hook);
        if (ret)
            goto unhook;
    }
    return 0;
unhook:
    /* Upstream's failed-install cleanup also removes its status hook. */
    WRITE_ONCE(status_armed, false);
    return ret;
}

static int selinux_hide_feature_get(u64 *value)
{
    /* Unlike upstream's requested-state getter, report actual query activation. */
    *value = smp_load_acquire(&ksu_selinux_hide_running) ? 1 : 0;
    return 0;
}

static int selinux_hide_feature_set(u64 value)
{
    bool enable = value != 0;
    int ret = 0;

    mutex_lock(&selinux_hide_mutex);
    WRITE_ONCE(requested_enabled, enable);
    ksu_selinux_hide_enabled = enable;
    if (enable && !ksu_selinux_hide_running) {
        ret = ksu_selinux_hide_enable();
        if (!ret) {
            policy_exposed = true;
            smp_store_release(&ksu_selinux_hide_running, true);
        }
    } else if (!enable) {
        if (ksu_selinux_hide_running)
            WRITE_ONCE(status_armed, false);
        /* Sleeping callbacks and existing descriptors may still use the snapshot.
         * Keep hooks installed as passthrough; never free published policy memory. */
        smp_store_release(&ksu_selinux_hide_running, false);
    }
    mutex_unlock(&selinux_hide_mutex);
    return ret;
}

static const struct ksu_feature_handler selinux_hide_handler = {
    .feature_id = KSU_FEATURE_SELINUX_HIDE,
    .name = "selinux_hide",
    .get_handler = selinux_hide_feature_get,
    .set_handler = selinux_hide_feature_set,
};

void ksu_selinux_hide_handle_second_stage(void)
{
    initialize_fake_status();
    if (fake_status)
        static_key_disable(&fake_status_initialize_key.key);
    else
        pr_info("selinux_hide: status snapshot will retry before post-fs-data\n");
}

void ksu_selinux_hide_handle_post_fs_data(void)
{
    static_key_disable(&fake_status_initialize_key.key);
    if (!fake_status)
        pr_warn("selinux_hide: no status snapshot captured before post-fs-data\n");
}

void ksu_selinux_hide_init(void)
{
    int ret = ksu_register_feature_handler(&selinux_hide_handler);
    if (ret)
        pr_err("selinux_hide: feature registration failed: %d\n", ret);
    if (ksu_late_loaded)
        initialize_fake_status();
    else
        static_key_enable(&fake_status_initialize_key.key);
    hook_selinux_status_open();
}

static void destroy_backup_policy(void)
{
    if (!backup_sepolicy)
        return;
    sidtab_destroy(backup_sepolicy->sidtab);
    kfree(backup_sepolicy->sidtab);
    policydb_destroy(&backup_sepolicy->policydb);
    kfree(backup_sepolicy);
    backup_sepolicy = NULL;
}

void ksu_selinux_hide_boot_completed(void)
{
    mutex_lock(&selinux_hide_mutex);
    backup_attempted = true;
    if (!ksu_selinux_hide_running) {
        /* Match upstream re-enable/EAGAIN semantics without freeing data that
         * a sleeping callback may still hold after an earlier disable. */
        backup_usable = false;
        if (!policy_exposed)
            destroy_backup_policy();
        backup_error = -EAGAIN;
    }
    mutex_unlock(&selinux_hide_mutex);
}

void ksu_selinux_hide_exit(void)
{
    /* Pinned callbacks forbid normal module unload. This is reached only if no
     * callback was ever installed (or built-in cleanup is explicitly invoked). */
    if (WARN_ON_ONCE(module_pinned))
        return;
    ksu_unregister_feature_handler(KSU_FEATURE_SELINUX_HIDE);
    destroy_backup_policy();
    if (fake_status)
        __free_page(fake_status);
    fake_status = NULL;
}

/* Called with ksu_rules held, BEFORE the first live policy modification. */
void ksu_selinux_hide_backup_policy(void)
{
    struct selinux_policy *live, *copy = NULL;
    struct policy_file file;
    void *data = NULL;
    size_t len;
    int ret = -ENOMEM;

    mutex_lock(&selinux_hide_mutex);
    if (backup_attempted)
        goto out;
    backup_attempted = true;
    /* ksu_rules excludes KSU edits; policy_mutex also excludes stock reloads. */
    mutex_lock(&selinux_state.policy_mutex);
    live = rcu_dereference_protected(selinux_state.policy,
                                    lockdep_is_held(&selinux_state.policy_mutex));
    if (!live) {
        ret = -EINVAL;
        goto fail;
    }
    /* policydb_read adds self attributes; allow one bitmap entry per type. */
    len = live->policydb.len + (size_t)live->policydb.p_types.nprim * (sizeof(u32) + sizeof(u64));
    data = vmalloc(len);
    copy = kzalloc(sizeof(*copy), GFP_KERNEL);
    if (!data || !copy)
        goto fail;
    file.data = data;
    file.len = len;
    ret = policydb_write(&live->policydb, &file);
    if (ret)
        goto fail;
    len -= file.len;
    /* Before 6.18, Android writers may omit these policy flags. */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 18, 0)
#ifdef POLICYDB_CONFIG_ANDROID_NETLINK_ROUTE
    if (len >= 24 && live->policydb.android_netlink_route)
        *(__le32 *)((char *)data + 20) |= cpu_to_le32(POLICYDB_CONFIG_ANDROID_NETLINK_ROUTE);
#endif
#ifdef POLICYDB_CONFIG_ANDROID_NETLINK_GETNEIGH
    if (len >= 24 && live->policydb.android_netlink_getneigh)
        *(__le32 *)((char *)data + 20) |= cpu_to_le32(POLICYDB_CONFIG_ANDROID_NETLINK_GETNEIGH);
#endif
#endif
    file.data = data;
    file.len = len;
    ret = policydb_read(&copy->policydb, &file);
    if (ret)
        goto fail; /* policydb_read destroys its partially built policy itself. */
    copy->policydb.len = len;
    copy->latest_granting = live->latest_granting;
    copy->sidtab = kzalloc(sizeof(*copy->sidtab), GFP_KERNEL);
    if (!copy->sidtab) {
        ret = -ENOMEM;
        goto destroy_policy;
    }
    ret = policydb_load_isids(&copy->policydb, copy->sidtab);
    if (ret) {
        /* policydb_load_isids already destroys the table on failure. */
        kfree(copy->sidtab);
        goto destroy_policy;
    }
    backup_sepolicy = copy;
    backup_usable = true;
    backup_error = 0;
    pr_info("selinux_hide: original policy captured\n");
    goto out_policy;
destroy_policy:
    policydb_destroy(&copy->policydb);
fail:
    kfree(copy);
    backup_error = ret;
    pr_err("selinux_hide: snapshot failed: %d\n", ret);
out_policy:
    mutex_unlock(&selinux_state.policy_mutex);
out:
    vfree(data);
    mutex_unlock(&selinux_hide_mutex);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
/*
 * Caveat:  Mutates scontext.
 */
static int string_to_context_struct(struct policydb *pol, struct sidtab *sidtabp, char *scontext, struct context *ctx,
                                    u32 def_sid)
{
    struct role_datum *role;
    struct type_datum *typdatum;
    struct user_datum *usrdatum;
    char *scontextp, *p, oldc;
    int rc = 0;

    context_init(ctx);

    /* Parse the security context. */

    rc = -EINVAL;
    scontextp = scontext;

    /* Extract the user. */
    p = scontextp;
    while (*p && *p != ':')
        p++;

    if (*p == 0)
        goto out;

    *p++ = 0;

    usrdatum = symtab_search(&pol->p_users, scontextp);
    if (!usrdatum)
        goto out;

    ctx->user = usrdatum->value;

    /* Extract role. */
    scontextp = p;
    while (*p && *p != ':')
        p++;

    if (*p == 0)
        goto out;

    *p++ = 0;

    role = symtab_search(&pol->p_roles, scontextp);
    if (!role)
        goto out;
    ctx->role = role->value;

    /* Extract type. */
    scontextp = p;
    while (*p && *p != ':')
        p++;
    oldc = *p;
    *p++ = 0;

    typdatum = symtab_search(&pol->p_types, scontextp);
    if (!typdatum || typdatum->attribute)
        goto out;

    ctx->type = typdatum->value;

    rc = mls_context_to_sid(pol, oldc, p, ctx, sidtabp, def_sid);
    if (rc)
        goto out;

    /* Check the validity of the new context. */
    rc = -EINVAL;
    if (!policydb_context_isvalid(pol, ctx))
        goto out;
    rc = 0;
out:
    if (rc)
        context_destroy(ctx);
    return rc;
}

static int security_context_to_sid_with_policy(struct selinux_policy *policy, const char *scontext, u32 scontext_len,
                                               u32 *sid, u32 def_sid, gfp_t gfp_flags)
{
    struct policydb *policydb;
    struct sidtab *sidtab;
    char *scontext2, *str = NULL;
    struct context context;
    int rc = 0;

    /* An empty security context is never valid. */
    if (!scontext_len)
        return -EINVAL;

    /* Copy the string to allow changes and ensure a NUL terminator */
    scontext2 = kmemdup_nul(scontext, scontext_len, gfp_flags);
    if (!scontext2)
        return -ENOMEM;

    // removed: if (!selinux_initialized())
    *sid = SECSID_NULL;

    // removed: if (force)
    // removed: rcu lock
    policydb = &policy->policydb;
    sidtab = policy->sidtab;
    rc = string_to_context_struct(policydb, sidtab, scontext2, &context, def_sid);
    if (rc)
        goto out;
    rc = sidtab_context_to_sid(sidtab, &context, sid);
    /* The private table is never frozen. Release context even if SID allocation fails. */
    context_destroy(&context);
out:
    kfree(scontext2);
    kfree(str);
    return rc;
}

/*
 * Write the security context string representation of
 * the context structure `context' into a dynamically
 * allocated string of the correct size.  Set `*scontext'
 * to point to this string and set `*scontext_len' to
 * the length of the string.
 */
static int context_struct_to_string(struct policydb *p, struct context *context, char **scontext, u32 *scontext_len)
{
    char *scontextp;

    if (scontext)
        *scontext = NULL;
    *scontext_len = 0;

    if (context->len) {
        *scontext_len = context->len;
        if (scontext) {
            *scontext = kstrdup(context->str, GFP_ATOMIC);
            if (!(*scontext))
                return -ENOMEM;
        }
        return 0;
    }

    /* Compute the size of the context. */
    *scontext_len += strlen(sym_name(p, SYM_USERS, context->user - 1)) + 1;
    *scontext_len += strlen(sym_name(p, SYM_ROLES, context->role - 1)) + 1;
    *scontext_len += strlen(sym_name(p, SYM_TYPES, context->type - 1)) + 1;
    *scontext_len += mls_compute_context_len(p, context);

    if (!scontext)
        return 0;

    /* Allocate space for the context; caller must free this space. */
    scontextp = kmalloc(*scontext_len, GFP_ATOMIC);
    if (!scontextp)
        return -ENOMEM;
    *scontext = scontextp;

    /*
     * Copy the user name, role name and type name into the context.
     */
    scontextp += sprintf(scontextp, "%s:%s:%s", sym_name(p, SYM_USERS, context->user - 1),
                         sym_name(p, SYM_ROLES, context->role - 1), sym_name(p, SYM_TYPES, context->type - 1));

    mls_sid_to_context(p, context, &scontextp);

    *scontextp = 0;

    return 0;
}

static int sidtab_entry_to_string(struct policydb *p, struct sidtab *sidtab, struct sidtab_entry *entry,
                                  char **scontext, u32 *scontext_len)
{
    int rc = sidtab_sid2str_get(sidtab, entry, scontext, scontext_len);

    if (rc != -ENOENT)
        return rc;

    rc = context_struct_to_string(p, &entry->context, scontext, scontext_len);
    if (!rc && scontext)
        sidtab_sid2str_put(sidtab, entry, *scontext, *scontext_len);
    return rc;
}

static int security_sid_to_context_with_policy(struct selinux_policy *policy, u32 sid, char **scontext,
                                               u32 *scontext_len)
{
    struct policydb *policydb;
    struct sidtab *sidtab;
    struct sidtab_entry *entry;
    int rc = 0;

    if (scontext)
        *scontext = NULL;
    *scontext_len = 0;

    // removed: if (!selinux_initialized())
    // removed: rcu lock
    policydb = &policy->policydb;
    sidtab = policy->sidtab;

    // removed: force
    entry = sidtab_search_entry(sidtab, sid);
    if (!entry) {
        pr_err("SELinux: %s:  unrecognized SID %d\n", __func__, sid);
        rc = -EINVAL;
        goto out_unlock;
    }
    // removed: only_invalid

    rc = sidtab_entry_to_string(policydb, sidtab, entry, scontext, scontext_len);

out_unlock:
    return rc;
}

static void avd_init(struct selinux_policy *policy, struct av_decision *avd)
{
    avd->allowed = 0;
    avd->auditallow = 0;
    avd->auditdeny = 0xffffffff;
    if (policy)
        avd->seqno = policy->latest_granting;
    else
        avd->seqno = 0;
    avd->flags = 0;
}

static void context_struct_compute_av(struct policydb *policydb, struct context *scontext, struct context *tcontext,
                                      u16 tclass, struct av_decision *avd, struct extended_perms *xperms);

/*
 * security_boundary_permission - drops violated permissions
 * on boundary constraint.
 */
static void __nocfi type_attribute_bounds_av(struct policydb *policydb, struct context *scontext,
                                             struct context *tcontext, u16 tclass, struct av_decision *avd)
{
    struct context lo_scontext;
    struct context lo_tcontext, *tcontextp = tcontext;
    struct av_decision lo_avd;
    struct type_datum *source;
    struct type_datum *target;
    u32 masked = 0;

    source = policydb->type_val_to_struct[scontext->type - 1];
    BUG_ON(!source);

    if (!source->bounds)
        return;

    target = policydb->type_val_to_struct[tcontext->type - 1];
    BUG_ON(!target);

    memset(&lo_avd, 0, sizeof(lo_avd));

    memcpy(&lo_scontext, scontext, sizeof(lo_scontext));
    lo_scontext.type = source->bounds;

    if (target->bounds) {
        memcpy(&lo_tcontext, tcontext, sizeof(lo_tcontext));
        lo_tcontext.type = target->bounds;
        tcontextp = &lo_tcontext;
    }

    context_struct_compute_av(policydb, &lo_scontext, tcontextp, tclass, &lo_avd, NULL);

    masked = ~lo_avd.allowed & avd->allowed;

    if (likely(!masked))
        return; /* no masked permission */

    /* mask violated permissions */
    avd->allowed &= ~masked;

    /* audit masked permissions */
    if (security_dump_masked_av_fn)
        security_dump_masked_av_fn(policydb, scontext, tcontext, tclass, masked, "bounds");
}

/*
 * Return the boolean value of a constraint expression
 * when it is applied to the specified source and target
 * security contexts.
 *
 * xcontext is a special beast...  It is used by the validatetrans rules
 * only.  For these rules, scontext is the context before the transition,
 * tcontext is the context after the transition, and xcontext is the context
 * of the process performing the transition.  All other callers of
 * constraint_expr_eval should pass in NULL for xcontext.
 */
static int constraint_expr_eval(struct policydb *policydb, struct context *scontext, struct context *tcontext,
                                struct context *xcontext, struct constraint_expr *cexpr)
{
    u32 val1, val2;
    struct context *c;
    struct role_datum *r1, *r2;
    struct mls_level *l1, *l2;
    struct constraint_expr *e;
    int s[CEXPR_MAXDEPTH];
    int sp = -1;

    for (e = cexpr; e; e = e->next) {
        switch (e->expr_type) {
        case CEXPR_NOT:
            BUG_ON(sp < 0);
            s[sp] = !s[sp];
            break;
        case CEXPR_AND:
            BUG_ON(sp < 1);
            sp--;
            s[sp] &= s[sp + 1];
            break;
        case CEXPR_OR:
            BUG_ON(sp < 1);
            sp--;
            s[sp] |= s[sp + 1];
            break;
        case CEXPR_ATTR:
            if (sp == (CEXPR_MAXDEPTH - 1))
                return 0;
            switch (e->attr) {
            case CEXPR_USER:
                val1 = scontext->user;
                val2 = tcontext->user;
                break;
            case CEXPR_TYPE:
                val1 = scontext->type;
                val2 = tcontext->type;
                break;
            case CEXPR_ROLE:
                val1 = scontext->role;
                val2 = tcontext->role;
                r1 = policydb->role_val_to_struct[val1 - 1];
                r2 = policydb->role_val_to_struct[val2 - 1];
                switch (e->op) {
                case CEXPR_DOM:
                    s[++sp] = ebitmap_get_bit(&r1->dominates, val2 - 1);
                    continue;
                case CEXPR_DOMBY:
                    s[++sp] = ebitmap_get_bit(&r2->dominates, val1 - 1);
                    continue;
                case CEXPR_INCOMP:
                    s[++sp] =
                        (!ebitmap_get_bit(&r1->dominates, val2 - 1) && !ebitmap_get_bit(&r2->dominates, val1 - 1));
                    continue;
                default:
                    break;
                }
                break;
            case CEXPR_L1L2:
                l1 = &(scontext->range.level[0]);
                l2 = &(tcontext->range.level[0]);
                goto mls_ops;
            case CEXPR_L1H2:
                l1 = &(scontext->range.level[0]);
                l2 = &(tcontext->range.level[1]);
                goto mls_ops;
            case CEXPR_H1L2:
                l1 = &(scontext->range.level[1]);
                l2 = &(tcontext->range.level[0]);
                goto mls_ops;
            case CEXPR_H1H2:
                l1 = &(scontext->range.level[1]);
                l2 = &(tcontext->range.level[1]);
                goto mls_ops;
            case CEXPR_L1H1:
                l1 = &(scontext->range.level[0]);
                l2 = &(scontext->range.level[1]);
                goto mls_ops;
            case CEXPR_L2H2:
                l1 = &(tcontext->range.level[0]);
                l2 = &(tcontext->range.level[1]);
                goto mls_ops;
            mls_ops:
                switch (e->op) {
                case CEXPR_EQ:
                    s[++sp] = mls_level_eq(l1, l2);
                    continue;
                case CEXPR_NEQ:
                    s[++sp] = !mls_level_eq(l1, l2);
                    continue;
                case CEXPR_DOM:
                    s[++sp] = mls_level_dom(l1, l2);
                    continue;
                case CEXPR_DOMBY:
                    s[++sp] = mls_level_dom(l2, l1);
                    continue;
                case CEXPR_INCOMP:
                    s[++sp] = mls_level_incomp(l2, l1);
                    continue;
                default:
                    BUG();
                    return 0;
                }
                break;
            default:
                BUG();
                return 0;
            }

            switch (e->op) {
            case CEXPR_EQ:
                s[++sp] = (val1 == val2);
                break;
            case CEXPR_NEQ:
                s[++sp] = (val1 != val2);
                break;
            default:
                BUG();
                return 0;
            }
            break;
        case CEXPR_NAMES:
            if (sp == (CEXPR_MAXDEPTH - 1))
                return 0;
            c = scontext;
            if (e->attr & CEXPR_TARGET)
                c = tcontext;
            else if (e->attr & CEXPR_XTARGET) {
                c = xcontext;
                if (!c) {
                    BUG();
                    return 0;
                }
            }
            if (e->attr & CEXPR_USER)
                val1 = c->user;
            else if (e->attr & CEXPR_ROLE)
                val1 = c->role;
            else if (e->attr & CEXPR_TYPE)
                val1 = c->type;
            else {
                BUG();
                return 0;
            }

            switch (e->op) {
            case CEXPR_EQ:
                s[++sp] = ebitmap_get_bit(&e->names, val1 - 1);
                break;
            case CEXPR_NEQ:
                s[++sp] = !ebitmap_get_bit(&e->names, val1 - 1);
                break;
            default:
                BUG();
                return 0;
            }
            break;
        default:
            BUG();
            return 0;
        }
    }

    BUG_ON(sp != 0);
    return s[0];
}

/*
 * Compute access vectors and extended permissions based on a context
 * structure pair for the permissions in a particular class.
 */
static void context_struct_compute_av(struct policydb *policydb, struct context *scontext, struct context *tcontext,
                                      u16 tclass, struct av_decision *avd, struct extended_perms *xperms)
{
    struct constraint_node *constraint;
    struct role_allow *ra;
    struct avtab_key avkey;
    struct avtab_node *node;
    struct class_datum *tclass_datum;
    struct ebitmap *sattr, *tattr;
    struct ebitmap_node *snode, *tnode;
    unsigned int i, j;

    avd->allowed = 0;
    avd->auditallow = 0;
    avd->auditdeny = 0xffffffff;
    if (xperms) {
        memset(&xperms->drivers, 0, sizeof(xperms->drivers));
        xperms->len = 0;
    }

    if (unlikely(!tclass || tclass > policydb->p_classes.nprim)) {
        pr_warn_ratelimited("SELinux:  Invalid class %u\n", tclass);
        return;
    }

    tclass_datum = policydb->class_val_to_struct[tclass - 1];

    /*
     * If a specific type enforcement rule was defined for
     * this permission check, then use it.
     */
    avkey.target_class = tclass;
    avkey.specified = AVTAB_AV | AVTAB_XPERMS;
    sattr = &policydb->type_attr_map_array[scontext->type - 1];
    tattr = &policydb->type_attr_map_array[tcontext->type - 1];
    ebitmap_for_each_positive_bit(sattr, snode, i)
    {
        ebitmap_for_each_positive_bit(tattr, tnode, j)
        {
            avkey.source_type = i + 1;
            avkey.target_type = j + 1;
            for (node = avtab_search_node(&policydb->te_avtab, &avkey); node;
                 node = avtab_search_node_next(node, avkey.specified)) {
                if (node->key.specified == AVTAB_ALLOWED)
                    avd->allowed |= node->datum.u.data;
                else if (node->key.specified == AVTAB_AUDITALLOW)
                    avd->auditallow |= node->datum.u.data;
                else if (node->key.specified == AVTAB_AUDITDENY)
                    avd->auditdeny &= node->datum.u.data;
                else if (xperms && (node->key.specified & AVTAB_XPERMS))
                    services_compute_xperms_drivers(xperms, node);
            }

            /* Check conditional av table for additional permissions */
            cond_compute_av(&policydb->te_cond_avtab, &avkey, avd, xperms);
        }
    }

    /*
     * Remove any permissions prohibited by a constraint (this includes
     * the MLS policy).
     */
    constraint = tclass_datum->constraints;
    while (constraint) {
        if ((constraint->permissions & (avd->allowed)) &&
            !constraint_expr_eval(policydb, scontext, tcontext, NULL, constraint->expr)) {
            avd->allowed &= ~(constraint->permissions);
        }
        constraint = constraint->next;
    }

    /*
     * If checking process transition permission and the
     * role is changing, then check the (current_role, new_role)
     * pair.
     */
    if (tclass == policydb->process_class && (avd->allowed & policydb->process_trans_perms) &&
        scontext->role != tcontext->role) {
        for (ra = policydb->role_allow; ra; ra = ra->next) {
            if (scontext->role == ra->role && tcontext->role == ra->new_role)
                break;
        }
        if (!ra)
            avd->allowed &= ~policydb->process_trans_perms;
    }

    /*
     * If the given source and target types have boundary
     * constraint, lazy checks have to mask any violated
     * permission and notice it to userspace via audit.
     */
    type_attribute_bounds_av(policydb, scontext, tcontext, tclass, avd);
}

static void __nocfi security_compute_av_user_with_policy(struct selinux_policy *policy, u32 ssid, u32 tsid, u16 tclass,
                                                         struct av_decision *avd)
{
    struct policydb *policydb;
    struct sidtab *sidtab;
    struct context *scontext = NULL, *tcontext = NULL;

    // remove: rcu lock
    avd_init(policy, avd);
    // remove: if (!selinux_initialized())

    policydb = &policy->policydb;
    sidtab = policy->sidtab;

    scontext = sidtab_search(sidtab, ssid);
    if (!scontext) {
        pr_err("SELinux: %s:  unrecognized SID %d\n", __func__, ssid);
        goto out;
    }

    /* permissive domain? */
    if (ebitmap_get_bit(&policydb->permissive_map, scontext->type))
        avd->flags |= AVD_FLAGS_PERMISSIVE;

    tcontext = sidtab_search(sidtab, tsid);
    if (!tcontext) {
        pr_err("SELinux: %s:  unrecognized SID %d\n", __func__, tsid);
        goto out;
    }

    if (unlikely(!tclass)) {
        if (policydb->allow_unknown)
            goto allow;
        goto out;
    }

    if (context_struct_compute_av_fn) {
        context_struct_compute_av_fn(policydb, scontext, tcontext, tclass, avd, NULL);
    } else {
        context_struct_compute_av(policydb, scontext, tcontext, tclass, avd, NULL);
    }
out:
    return;
allow:
    avd->allowed = 0xffffffff;
    goto out;
}
#endif
