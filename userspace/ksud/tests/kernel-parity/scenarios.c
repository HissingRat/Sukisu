static void reset(void) {
    if(fake_status) free(fake_status);
    fake_status=NULL;memset(&selinux_state,0,sizeof(selinux_state));
    memset(&fake_state,0,sizeof(fake_state));memset(&live_page,0,sizeof(live_page));
    live_page.status=(struct selinux_kernel_status){1,8,1,7,1};
    live_policy.latest_granting=99;backup_policy.latest_granting=37;
    selinux_state.policy=&live_policy;fake_state.policy=&backup_policy;
    fixture_uid=10001;permission_error=0;original_open_error=0;
    fail_allocation=false;create_on_open=false;allocations=0;original_calls=0;context_syncs=0;
    original_size=0;live_sids_seen=0;memset(original_value,0,sizeof(original_value));memset(original_name,0,sizeof(original_name));
    ksu_late_loaded=false;ksu_selinux_hide_enabled=true;ksu_selinux_hide_running=true;
    requested_enabled=true;status_armed=true;fake_status_initialize_key.key.enabled=true;
}
static void hex(const char *value,size_t length) {
    for(size_t i=0;i<length;i++) printf("%02x",(unsigned char)value[i]);
}
static void access_cases(void) {
    const char *inputs[]={"u:r:app:s0 u:object_r:stock_file:s0 12", "u:r:app:s0 u:object_r:ksu_file:s0 12", "invalid u:object_r:stock_file:s0 12", "u:r:app:s0", "u:r:app:s0 u:object_r:stock_file:s0 65536"};
    for(unsigned system=0;system<2;system++)for(unsigned deny=0;deny<2;deny++)for(unsigned i=0;i<sizeof(inputs)/sizeof(*inputs);i++) {
        reset();fixture_uid=system?1000:10001;permission_error=deny?-EACCES:0;
        char input[4096];strcpy(input,inputs[i]);struct file file={0};
        ssize_t result=my_write_access(&file,input,strlen(input));
        printf("access system=%u deny=%u case=%u ret=%zd orig=%u sync=%u live_sids=%u output=",system,deny,i,result,original_calls,context_syncs,live_sids_seen);
        if(result>=0 && result!=987) hex(input,(size_t)result);
        printf("\n");
    }
}
static void context_cases(void) {
    const char *inputs[]={"u:r:app:s0","u:object_r:ksu_file:s0","invalid"};
    for(unsigned system=0;system<2;system++)for(unsigned deny=0;deny<2;deny++)for(unsigned i=0;i<3;i++) {
        reset();fixture_uid=system?1000:10001;permission_error=deny?-EACCES:0;
        char input[4096];strcpy(input,inputs[i]);struct file file={0};
        ssize_t result=my_write_context(&file,input,strlen(input));
        printf("context system=%u deny=%u case=%u ret=%zd orig=%u sync=%u live_sids=%u output=",system,deny,i,result,original_calls,context_syncs,live_sids_seen);
        if(result>=0 && result!=987) hex(input,(size_t)result);
        printf("\n");
    }
}
static void setprocattr_cases(void) {
    const char *inputs[]={"u:r:app:s0","u:r:app:s0\n","u:object_r:ksu_file:s0\n","invalid","invalid\n","\n",""};
    const char *names[]={"current","exec"};
    for(unsigned system=0;system<2;system++)for(unsigned deny=0;deny<2;deny++)for(unsigned name=0;name<2;name++)for(unsigned i=0;i<7;i++) {
        reset();fixture_uid=system?1000:10001;permission_error=deny?-EPERM:0;
        char input[4096];strcpy(input,inputs[i]);size_t size=strlen(input);
        int result=my_setprocattr(names[name],input,size);
        printf("setprocattr system=%u deny=%u name=%u case=%u ret=%d orig=%u origsize=%zu origattr=%s forwarded=",system,deny,name,i,result,original_calls,original_size,original_name);
        if(original_calls) hex(original_value,original_size+1);
        printf(" mutated=");hex(input,size+1);printf("\n");
    }
}
static void print_status(const char *stage,struct file *file,int result) {
    printf("status stage=%s ret=%d orig=%u allocations=%u retry=%u selected=%s snapshot=",stage,result,original_calls,allocations,fake_status_initialize_key.key.enabled,
        !file||!file->private_data?"none":file->private_data==fake_status?"fake":"live");
    if(fake_status) {
        struct selinux_kernel_status *s=&fake_status->status;
        printf("%u,%u,%u,%u,%u",s->version,s->sequence,s->enforcing,s->policyload,s->deny_unknown);
    }else printf("none");
    printf("\n");
}
static void status_cases(void) {
    reset();selinux_state.status_page=&live_page;initialize_fake_status();print_status("early-enforcing",NULL,0);
    live_page.status.sequence=100;live_page.status.policyload=100;initialize_fake_status();print_status("immutable",NULL,0);
    struct file file={0};int result=my_sel_open_handle_status(NULL,&file);print_status("app-existing-snapshot",&file,result);
    fixture_uid=1000;file.private_data=NULL;result=my_sel_open_handle_status(NULL,&file);print_status("system",&file,result);
    fixture_uid=10001;requested_enabled=false;ksu_selinux_hide_enabled=false;file.private_data=NULL;
    result=my_sel_open_handle_status(NULL,&file);print_status("disabled",&file,result);

    reset();selinux_state.status_page=&live_page;live_page.status.enforcing=0;initialize_fake_status();print_status("early-not-enforcing",NULL,0);
    live_page.status.enforcing=1;initialize_fake_status();print_status("enforcement-retry",NULL,0);

    reset();ksu_late_loaded=true;selinux_state.status_page=&live_page;live_page.status.enforcing=0;initialize_fake_status();print_status("late-not-enforcing-normalization",NULL,0);
    reset();ksu_late_loaded=true;selinux_state.status_page=&live_page;initialize_fake_status();print_status("late-enforcing-normalization",NULL,0);
    reset();ksu_late_loaded=true;selinux_state.status_page=&live_page;live_page.status.enforcing=2;initialize_fake_status();print_status("late-noncanonical-nonzero-enforcing-preserved",NULL,0);

    reset();initialize_fake_status();print_status("missing-page",NULL,0);
    create_on_open=true;live_page.status.enforcing=0;file.private_data=NULL;result=my_sel_open_handle_status(NULL,&file);print_status("open-creates-not-enforcing",&file,result);
    live_page.status.enforcing=1;file.private_data=NULL;result=my_sel_open_handle_status(NULL,&file);print_status("open-retry-returns-original",&file,result);
    file.private_data=NULL;result=my_sel_open_handle_status(NULL,&file);print_status("next-open-selects-snapshot",&file,result);

    reset();selinux_state.status_page=&live_page;fail_allocation=true;initialize_fake_status();print_status("allocation-failure",NULL,0);
    fail_allocation=false;initialize_fake_status();print_status("allocation-retry",NULL,0);

    reset();original_open_error=-ENOMEM;create_on_open=true;file.private_data=NULL;result=my_sel_open_handle_status(NULL,&file);print_status("failed-open-does-not-capture",&file,result);

    reset();ksu_selinux_hide_handle_second_stage();print_status("second-stage-no-page-keeps-retry",NULL,0);
    ksu_selinux_hide_handle_post_fs_data();print_status("post-fs-cutoff",NULL,0);
    create_on_open=true;file.private_data=NULL;result=my_sel_open_handle_status(NULL,&file);print_status("after-cutoff-no-capture",&file,result);
    reset();selinux_state.status_page=&live_page;ksu_selinux_hide_handle_second_stage();print_status("second-stage-captured-stops-retry",NULL,0);
}
int main(void) { access_cases();context_cases();setprocattr_cases();status_cases();if(fake_status)free(fake_status);return 0; }
