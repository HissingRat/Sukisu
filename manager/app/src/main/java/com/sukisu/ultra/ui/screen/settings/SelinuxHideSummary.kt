package com.sukisu.ultra.ui.screen.settings

import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import com.sukisu.ultra.R
import com.sukisu.ultra.ui.viewmodel.SettingsUiState

@Composable
internal fun selinuxHideSummary(state: SettingsUiState): String = when {
    state.selinuxHideError != null -> state.selinuxHideError
    state.selinuxHideStatus == "unsupported" -> stringResource(R.string.feature_status_unsupported_summary)
    state.selinuxHideStatus == "managed" -> stringResource(R.string.feature_status_managed_summary)
    state.selinuxHidePendingReboot && state.selinuxHideRequestedEnabled == true ->
        stringResource(R.string.settings_selinux_hide_pending_enable)
    state.selinuxHidePendingReboot -> stringResource(R.string.settings_selinux_hide_pending_disable)
    state.isSelinuxHideEnabled -> stringResource(R.string.settings_selinux_hide_active)
    else -> stringResource(R.string.settings_selinux_hide_summary)
}
