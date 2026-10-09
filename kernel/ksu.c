#include <linux/export.h>
#include <linux/fs.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/workqueue.h>

#include "allowlist.h"
#include "feature.h"
#include "selinux_hide.h"
#include "klog.h" // IWYU pragma: keep
#include "throne_tracker.h"
#include "syscall_hook_manager.h"
#include "ksud.h"
#include "supercalls.h"
#include "ksu.h"
#include "file_wrapper.h"
#include "infra/symbol_resolver.h"
#include "hook/lsm_hook.h"
#include "selinux/selinux.h"
#include "app_profile.h"
#include "manager.h"
#include "security.h"
#include <linux/rcupdate.h>

// workaround for A12-5.10 kernel
// Some third-party kernel (e.g. linegaeOS) uses wrong toolchain, which supports
// CC_HAVE_STACKPROTECTOR_SYSREG while gki's toolchain doesn't.
// Therefore, ksu lkm, which uses gki toolchain, requires this __stack_chk_guard,
// while those third-party kernel can't provide.
// Thus, we manually provide it instead of using kernel's
#if defined(CONFIG_ARM64) && defined(CONFIG_STACKPROTECTOR) && !defined(CONFIG_STACKPROTECTOR_PER_TASK)
#include <linux/stackprotector.h>
#include <linux/random.h>
unsigned long __stack_chk_guard __ro_after_init
    __attribute__((visibility("hidden")));
#define NO_STACK_PROTECTOR_WORKAROUND __attribute__((no_stack_protector))
#else
#define NO_STACK_PROTECTOR_WORKAROUND
#endif

struct cred *ksu_cred;
bool ksu_late_loaded;

void sukisu_custom_config_init(void)
{
}

void sukisu_custom_config_exit(void)
{
}

NO_STACK_PROTECTOR_WORKAROUND
int __init kernelsu_init(void)
{
#if defined(CONFIG_ARM64) && defined(CONFIG_STACKPROTECTOR) && !defined(CONFIG_STACKPROTECTOR_PER_TASK)
    unsigned long canary;

    /* Try to get a semi random initial value. */
    get_random_bytes(&canary, sizeof(canary));
    canary ^= LINUX_VERSION_CODE;
    canary &= CANARY_MASK;
    __stack_chk_guard = canary;
#endif

#ifdef CONFIG_KSU_DEBUG
    pr_alert("*************************************************************");
    pr_alert("**     NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE    **");
    pr_alert("**                                                         **");
    pr_alert("**         You are running KernelSU in DEBUG mode          **");
    pr_alert("**                                                         **");
    pr_alert("**     NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE    **");
    pr_alert("*************************************************************");
#endif

    ksu_cred = prepare_creds();
    if (!ksu_cred) {
        pr_err("prepare cred failed!\n");
        return -ENOMEM;
    }

#ifdef MODULE
    /* A non-init loader can also run during early boot, before any policy exists.
     * Keep that case on the early hooks instead of dereferencing a NULL policy. */
    ksu_late_loaded = current->pid != 1 && rcu_access_pointer(selinux_state.policy);
#else
    ksu_late_loaded = false;
#endif
    ksu_init_symbol_resolver();
    ksu_lsm_hook_init();
    ksu_feature_init();

    ksu_selinux_hide_init();

    ksu_supercalls_init();

    sukisu_custom_config_init();

    if (ksu_late_loaded) {
        /* Match upstream late-load bootstrap before any user feature request. */
        apply_kernelsu_rules();
        cache_sid();
        setup_ksu_cred();
        escape_to_root_for_init();
        ksu_allowlist_init();
        ksu_load_allow_list();
        ksu_syscall_hook_manager_init();
        ksu_throne_tracker_init();
        ksu_observer_init();
        ksu_file_wrapper_init();
        ksu_boot_completed = true;
        track_throne(false);
        if (!getenforce())
            setenforce(true);
    } else {
        ksu_syscall_hook_manager_init();
        ksu_allowlist_init();
        ksu_throne_tracker_init();
        ksu_ksud_init();
        ksu_file_wrapper_init();
    }

#ifdef MODULE
#ifndef CONFIG_KSU_DEBUG
    kobject_del(&THIS_MODULE->mkobj.kobj);
#endif
#endif
    return 0;
}

extern void ksu_observer_exit(void);
void kernelsu_exit(void)
{
    ksu_allowlist_exit();

    ksu_throne_tracker_exit();

    ksu_observer_exit();

    if (!ksu_late_loaded)
        ksu_ksud_exit();

    ksu_syscall_hook_manager_exit();

    sukisu_custom_config_exit();

    ksu_supercalls_exit();

    ksu_selinux_hide_exit();

    ksu_feature_exit();

    if (ksu_cred) {
        put_cred(ksu_cred);
    }
}

module_init(kernelsu_init);
module_exit(kernelsu_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("weishu");
MODULE_DESCRIPTION("Android KernelSU");
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 13, 0)
MODULE_IMPORT_NS("VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver");
#else
MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
#endif
