package com.sukisu.ultra.ui.viewmodel

import com.sukisu.ultra.ui.UiMode

data class SettingsUiState(
    val uiMode: String = UiMode.DEFAULT_VALUE,
    val checkUpdate: Boolean = true,
    val checkModuleUpdate: Boolean = true,
    val alternativeIcon : Boolean = false,
    val themeMode: Int = 0,
    val keyColor: Int = 0,
    val enablePredictiveBack: Boolean = false,
    val enableBlur: Boolean = true,
    val enableFloatingBottomBar: Boolean = false,
    val enableFloatingBottomBarBlur: Boolean = false,
    val pageScale: Float = 1.0f,
    val enableWebDebugging: Boolean = false,

    // Su Compat
    val suCompatStatus: String = "",
    val suCompatMode: Int = 0, // 0: enable default, 1: disable until reboot, 2: disable always
    val isSuEnabled: Boolean = false,

    // Kernel Umount
    val kernelUmountStatus: String = "",
    val isKernelUmountEnabled: Boolean = false,

    // The switch represents the active context/access query group. A requested enable
    // can already mask SELinux status while the core group still requires reboot.
    val selinuxHideStatus: String = "",
    val isSelinuxHideEnabled: Boolean = false,
    val selinuxHideRequestedEnabled: Boolean? = null,
    val selinuxHideBusy: Boolean = false,
    val selinuxHideError: String? = null,

    // Umount Modules
    val isDefaultUmountModules: Boolean = false,

    val isLkmMode: Boolean = false
) {
    // Requested status-only hiding has no separate feature getter. Always provide
    // an explicit reset when core queries are inactive, including after app relaunch.
    val selinuxHideCanClearRequest: Boolean
        get() = selinuxHideStatus == "supported" && !isSelinuxHideEnabled

    val selinuxHidePendingReboot: Boolean
        get() = selinuxHideRequestedEnabled != null &&
            selinuxHideRequestedEnabled != isSelinuxHideEnabled
}
