/* Hook patching and module ownership are external services, not reimplemented controller logic. */
#define KSU_PATCH_TEXT_FLUSH_DCACHE 0
static int selinux_hide_mutex;
static bool backup_usable, backup_attempted, policy_exposed, module_pinned;
static int backup_error;
static bool context_installed, access_installed, status_installed;
static bool missing_write_ops, missing_status_ops;
static unsigned patch_calls, fail_patch_call;
static void *security_dump_masked_av_fn, *context_struct_compute_av_fn;
enum { SEL_CONTEXT=5, SEL_ACCESS=6 };
static write_op_fn mock_write_ops[16];
static write_op_fn *selinux_write_op, *context_write, *access_write;
typedef int (*sel_open_handle_status_fn)(struct inode*,struct file*);
static sel_open_handle_status_fn *sel_open_handle_status_slot;
static struct file_operations { sel_open_handle_status_fn open; } mock_status_ops;
static void pin_callbacks(void) { module_pinned=true; }
static void *find_kernel_symbol_exact(const char *name) {
    if(!strcmp(name,"write_op"))return missing_write_ops?NULL:mock_write_ops;
    if(!strcmp(name,"sel_handle_status_ops"))return missing_status_ops?NULL:&mock_status_ops;
    return NULL;
}
static int ksu_patch_text(void *target,const void *source,size_t size,int flags) {
    (void)flags;if (++patch_calls==fail_patch_call) return -ENOMEM;
    memcpy(target,source,size);return 0;
}
static int ksu_lsm_hook(typeof(selinux_setprocattr_hook)*hook) { hook->entry=(void*)1;return 0; }
static void ksu_lsm_unhook(typeof(selinux_setprocattr_hook)*hook) { hook->entry=NULL; }
static void sidtab_destroy(void *sidtab) { (void)sidtab; }
static void policydb_destroy(struct policydb *policy) { (void)policy; }
static void ksu_destroy_sepolicy(struct selinux_policy *policy) { free(policy); }
static void hook_selinux_status_open(void);
static void ksu_selinux_hide_unhook(void);
