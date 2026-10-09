#include <assert.h>
static void control_reset(void) {
    if(backup_sepolicy && backup_sepolicy!=&backup_policy) {
        free(backup_sepolicy->sidtab);free(backup_sepolicy);
    }
    reset();
    backup_sepolicy=calloc(1,sizeof(*backup_sepolicy));backup_sepolicy->sidtab=calloc(1,4);
    backup_sepolicy->latest_granting=37;
    backup_usable=true;backup_error=0;backup_attempted=false;policy_exposed=false;module_pinned=false;
    context_installed=false;access_installed=false;status_installed=false;
    missing_write_ops=false;missing_status_ops=false;patch_calls=0;fail_patch_call=0;
    memset(mock_write_ops,0,sizeof(mock_write_ops));mock_write_ops[SEL_CONTEXT]=stock_transaction;mock_write_ops[SEL_ACCESS]=stock_transaction;
    context_write=NULL;access_write=NULL;selinux_write_op=NULL;sel_open_handle_status_slot=NULL;
    orig_context_write=NULL;orig_access_write=NULL;orig_sel_open_handle_status=NULL;
    mock_status_ops.open=stock_status_open;selinux_setprocattr_hook.entry=NULL;selinux_setprocattr_hook.original=stock_setprocattr;
    ksu_selinux_hide_running=false;ksu_selinux_hide_enabled=false;requested_enabled=false;
    selinux_state.status_page=&live_page;initialize_fake_status();hook_selinux_status_open();
}
static void complete_boot(void) {
#if TEST_BACKPORT
    ksu_selinux_hide_boot_completed();
#else
    ksu_selinux_hide_drop_backup_if_unused();
#endif
}
static void observe(const char *stage,int result) {
    u64 getter=99;assert(selinux_hide_feature_get(&getter)==0);
#if TEST_BACKPORT
    assert(getter==(u64)ksu_selinux_hide_running);
#else
    assert(getter==(u64)ksu_selinux_hide_enabled);
#endif
    struct file file={0};int open_result=mock_status_ops.open(NULL,&file);
    printf("control stage=%s ret=%d core=%u requested=%u status_result=%d status_selected=%s\n",
        stage,result,ksu_selinux_hide_running,ksu_selinux_hide_enabled,open_result,
        file.private_data==fake_status?"fake":"live");
}
static void control_cases(void) {
    // Snapshot availability is the externally controlled second-stage input here.
    // All requested/core/status transitions execute actual production controllers.
    control_reset();free(backup_sepolicy->sidtab);free(backup_sepolicy);backup_sepolicy=NULL;
    backup_usable=false;backup_error=-EAGAIN;
    observe("init-before-policy-snapshot",0);
    observe("initial-EAGAIN",selinux_hide_feature_set(1));
    observe("clear-initial-EAGAIN",selinux_hide_feature_set(0));
    observe("repeat-initial-EAGAIN",selinux_hide_feature_set(1));
    backup_sepolicy=calloc(1,sizeof(*backup_sepolicy));backup_sepolicy->sidtab=calloc(1,4);
    backup_sepolicy->latest_granting=37;backup_usable=true;backup_error=0;
    observe("capture-then-successful-enable",selinux_hide_feature_set(1));
    observe("disable-after-capture",selinux_hide_feature_set(0));

    control_reset();observe("raw-nonzero-2",selinux_hide_feature_set(2));
    complete_boot();observe("boot-while-active",0);
    observe("disable-after-active-boot",selinux_hide_feature_set(0));
    observe("reenable-after-active-boot",selinux_hide_feature_set(7));

    control_reset();observe("enable-before-unused-boot",selinux_hide_feature_set(1));
    observe("disable-before-unused-boot",selinux_hide_feature_set(0));
    complete_boot();observe("unused-boot-completed",0);
    observe("reenable-after-unused-boot",selinux_hide_feature_set(1));

    control_reset();complete_boot();observe("never-enabled-boot",0);
    observe("missing-backup-EAGAIN",selinux_hide_feature_set(1));
    observe("clear-EAGAIN-request",selinux_hide_feature_set(0));

    control_reset();missing_write_ops=true;
    observe("fatal-main-enable-error",selinux_hide_feature_set(1));
    observe("clear-fatal-request",selinux_hide_feature_set(0));

    control_reset();fail_patch_call=patch_calls+1;
    int patch_result=selinux_hide_feature_set(1);
#if TEST_BACKPORT
    assert(patch_result==-ENOMEM); // Intentional preservation of the actual patch error.
#else
    assert(patch_result==-ENOSYS); // Upstream collapses post-status installation errors.
#endif
    observe("post-status-patch-failure-error-class",patch_result<0?-1:0);
    observe("clear-post-status-patch-failure",selinux_hide_feature_set(0));

    control_reset();missing_status_ops=true;mock_status_ops.open=stock_status_open;
    orig_sel_open_handle_status=NULL;status_installed=false;sel_open_handle_status_slot=NULL;
    observe("optional-status-fallback",selinux_hide_feature_set(1));
}
