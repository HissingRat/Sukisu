/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef KSU_SELINUX_HIDE_H
#define KSU_SELINUX_HIDE_H

void ksu_selinux_hide_init(void);
void ksu_selinux_hide_exit(void);
void ksu_selinux_hide_backup_policy(void);
void ksu_selinux_hide_handle_second_stage(void);
void ksu_selinux_hide_handle_post_fs_data(void);
void ksu_selinux_hide_boot_completed(void);

#endif
