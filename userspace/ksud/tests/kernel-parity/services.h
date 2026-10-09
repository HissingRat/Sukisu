/* Controlled kernel services. Hook bodies are extracted verbatim by run.py. */
#include <stdbool.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/types.h>
// Kernel errno ABI is Linux even when this host fixture runs on macOS.
#undef EAGAIN
#undef ENOSYS
#undef EOPNOTSUPP
#define EAGAIN 11
#define ENOSYS 38
#define EOPNOTSUPP 95

typedef uint32_t u32;
typedef uint64_t u64;
typedef uint16_t u16;
typedef unsigned gfp_t;
#define __nocfi
#define likely(x) (x)
#define unlikely(x) (x)
#define KERNEL_VERSION(a,b,c) (((a)<<16)|((b)<<8)|(c))
#define LINUX_VERSION_CODE KERNEL_VERSION(TEST_KERNEL_MAJOR,TEST_KERNEL_MINOR,0)
#define GFP_KERNEL 0
#define __GFP_ZERO 0
#define SECSID_NULL 0
#define SECINITSID_SECURITY 1
#define SECCLASS_SECURITY 1
#define SECCLASS_PROCESS 2
#define SECURITY__CHECK_CONTEXT 1
#define SECURITY__COMPUTE_AV 2
#define PROCESS__SETCURRENT 3
#define SIMPLE_TRANSACTION_LIMIT 4096
#define pr_info(...) ((void)0)
#define pr_warn(...) ((void)0)
#define pr_err(...) ((void)0)
#define mutex_lock(x) ((void)(x))
#define mutex_unlock(x) ((void)(x))
#define kfree free
#define kzalloc(n,gfp) calloc(1,(n))
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define smp_load_acquire(x) (*(x))
#define smp_store_release(x,v) (*(x)=(v))

struct selinux_kernel_status { u32 version, sequence, enforcing, policyload, deny_unknown; };
struct page { struct selinux_kernel_status status; };
struct policydb { int unused; };
struct selinux_policy { u32 latest_granting; void *sidtab; struct policydb policydb; };
struct selinux_state { int status_lock; struct page *status_page; struct selinux_policy *policy; bool initialized; };
struct file { void *private_data; };
struct inode { int unused; };
struct av_decision { u32 allowed, auditallow, auditdeny, seqno, flags; };
struct kuid { unsigned val; };
struct static_key { bool enabled; };
struct static_key_container { struct static_key key; };
static struct static_key_container fake_status_initialize_key;
#define static_branch_unlikely(x) ((x)->key.enabled)
#define static_key_enable(x) ((x)->enabled = true)
#define static_key_disable(x) ((x)->enabled = false)

static struct selinux_state selinux_state, fake_state;
#define query_state fake_state
static struct selinux_policy live_policy, backup_policy;
static struct selinux_policy *backup_sepolicy = &backup_policy;
static struct page *fake_status;
#define status_snapshot fake_status
static bool ksu_late_loaded, ksu_selinux_hide_enabled, requested_enabled;
static bool ksu_selinux_hide_running, status_armed;
#define ksu_selinux_hide_requested requested_enabled
static unsigned fixture_uid;
static int permission_error, original_open_error;
static bool fail_allocation, create_on_open;
static unsigned allocations, original_calls, context_syncs;
static struct page live_page;
static char original_value[4096], original_name[32];
static unsigned live_sids_seen;
static size_t original_size;

static struct kuid current_uid(void) { return (struct kuid){fixture_uid}; }
static u32 current_sid(void) { return 100; }
static bool hide_for_current(void) { return ksu_selinux_hide_enabled && fixture_uid >= 10000; }
static void *page_address(struct page *page) { return &page->status; }
static struct page *alloc_page(unsigned flags) {
    (void)flags;
    if (fail_allocation) return NULL;
    allocations++;
    return calloc(1,sizeof(struct page));
}
static void __free_page(struct page *page) { free(page); }
static int scnprintf(char *buffer, size_t capacity, const char *format, ...) {
    va_list args; va_start(args,format); int count=vsnprintf(buffer,capacity,format,args); va_end(args);
    return count < (int)capacity ? count : (int)capacity-1;
}
static int avc_has_perm(struct selinux_state *state, u32 source, u32 target, u16 cls, u32 permission, void *audit) {
    assert(state == &selinux_state); // Permission decisions must remain live.
    (void)source;(void)target;(void)cls;(void)permission;(void)audit;
    return permission_error;
}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,6,0)
/* 6.6+ drops the state argument; fixtures keep the exact call signatures. */
#define avc_has_perm(source,target,cls,permission,audit) avc_has_perm(&selinux_state,source,target,cls,permission,audit)
#endif
static int context_sid(bool snapshot, const char *input, u32 length, u32 *sid) {
    if (!snapshot) context_syncs++;
    if (length == strlen("u:r:app:s0") && !memcmp(input,"u:r:app:s0",length)) { *sid=10;if(!snapshot)live_sids_seen|=1;return 0; }
    if (length == strlen("u:object_r:stock_file:s0") && !memcmp(input,"u:object_r:stock_file:s0",length)) { *sid=20;if(!snapshot)live_sids_seen|=2;return 0; }
    if (!snapshot && length == strlen("u:object_r:ksu_file:s0") && !memcmp(input,"u:object_r:ksu_file:s0",length)) { *sid=30;if(!snapshot)live_sids_seen|=4;return 0; }
    return -EINVAL;
}
static int security_context_to_sid(struct selinux_state *state,const char *input,u32 length,u32 *sid,gfp_t flags) {
    (void)flags;return context_sid(state==&fake_state,input,length,sid);
}
static int security_context_to_sid_with_policy(struct selinux_policy *policy,const char *input,u32 length,u32 *sid,u32 def,gfp_t flags) {
    (void)def;(void)flags;return context_sid(policy==backup_sepolicy,input,length,sid);
}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,6,0)
#define security_context_to_sid(input,length,sid,flags) security_context_to_sid(&selinux_state,input,length,sid,flags)
#endif
static int security_sid_to_context(struct selinux_state *state,u32 sid,char **result,u32 *length) {
    (void)state;const char *name=sid==10?"u:r:app:s0":"u:object_r:stock_file:s0";
    *result=strdup(name);*length=(u32)strlen(name)+1;return 0;
}
static int security_sid_to_context_with_policy(struct selinux_policy *policy,u32 sid,char **result,u32 *length) {
    (void)policy;return security_sid_to_context(&fake_state,sid,result,length);
}
static void security_compute_av_user(struct selinux_state *state,u32 source,u32 target,u16 cls,struct av_decision *decision) {
    (void)source;(void)target;(void)cls;
    *decision=(struct av_decision){.allowed=0x15,.auditallow=3,.auditdeny=0xff,.seqno=state->policy->latest_granting,.flags=0x20};
}
static void security_compute_av_user_with_policy(struct selinux_policy *policy,u32 source,u32 target,u16 cls,struct av_decision *decision) {
    struct selinux_state state={.policy=policy};security_compute_av_user(&state,source,target,cls,decision);
}
static ssize_t stock_transaction(struct file *file,char *buffer,size_t size) {
    (void)file;(void)buffer;(void)size;original_calls++;return 987;
}
typedef ssize_t (*write_op_fn)(struct file*,char*,size_t);
static write_op_fn orig_context_write=stock_transaction,orig_access_write=stock_transaction;
#define original_context orig_context_write
#define original_access orig_access_write
static int stock_setprocattr(const char *name,void *value,size_t size) {
    snprintf(original_name,sizeof(original_name),"%s",name);original_calls++;original_size=size;
    memcpy(original_value,value,size+1);return 123;
}
typedef int (*setprocattr_fn)(const char*,void*,size_t);
static struct { setprocattr_fn original; void *entry; } selinux_setprocattr_hook={stock_setprocattr,NULL};
static setprocattr_fn original_setprocattr=stock_setprocattr;
static int stock_status_open(struct inode *inode,struct file *file) {
    (void)inode;original_calls++;
    if (original_open_error) return original_open_error;
    if (create_on_open && !selinux_state.status_page) selinux_state.status_page=&live_page;
    file->private_data=selinux_state.status_page;return 0;
}
static int (*orig_sel_open_handle_status)(struct inode*,struct file*)=stock_status_open;
#define original_status_open orig_sel_open_handle_status
