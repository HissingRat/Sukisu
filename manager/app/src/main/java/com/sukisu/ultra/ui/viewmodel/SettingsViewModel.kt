package com.sukisu.ultra.ui.viewmodel

import android.system.OsConstants
import android.widget.Toast
import com.sukisu.ultra.R
import com.sukisu.ultra.ksuApp
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.withContext
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import com.sukisu.ultra.data.repository.SettingsRepository
import com.sukisu.ultra.data.repository.SettingsRepositoryImpl

class SettingsViewModel(
    private val repo: SettingsRepository = SettingsRepositoryImpl()
) : ViewModel() {

    private val _uiState = MutableStateFlow(SettingsUiState())
    val uiState: StateFlow<SettingsUiState> = _uiState.asStateFlow()

    init {
        refresh()
    }

    fun refresh() {
        viewModelScope.launch {
            val checkUpdate = repo.checkUpdate
            val checkModuleUpdate = repo.checkModuleUpdate
            val alternativeIcon = repo.alternativeIcon
            val themeMode = repo.themeMode
            val keyColor = repo.keyColor
            val enablePredictiveBack = repo.enablePredictiveBack
            val enableBlur = repo.enableBlur
            val enableFloatingBottomBar = repo.enableFloatingBottomBar
            val enableFloatingBottomBarBlur = repo.enableFloatingBottomBarBlur
            val pageScale = repo.pageScale
            val enableWebDebugging = repo.enableWebDebugging
            val isLkmMode = repo.isLkmMode()

            // Async loading for natives/features
            val suCompatStatus = repo.getSuCompatStatus()
            val suCompatPersistValue = repo.getSuCompatPersistValue()
            val isSuEnabled = repo.isSuEnabled()

            val suCompatMode = if (suCompatPersistValue == 0L) 2 else if (!isSuEnabled) 1 else 0

            val kernelUmountStatus = repo.getKernelUmountStatus()
            val isKernelUmountEnabled = repo.isKernelUmountEnabled()
            val isDefaultUmountModules = repo.isDefaultUmountModules()
            val uiMode = repo.uiMode

            _uiState.update {
                it.copy(
                    uiMode = uiMode,
                    checkUpdate = checkUpdate,
                    checkModuleUpdate = checkModuleUpdate,
                    alternativeIcon = alternativeIcon,
                    themeMode = themeMode,
                    keyColor = keyColor,
                    enablePredictiveBack = enablePredictiveBack,
                    enableBlur = enableBlur,
                    enableFloatingBottomBar = enableFloatingBottomBar,
                    enableFloatingBottomBarBlur = enableFloatingBottomBarBlur,
                    pageScale = pageScale,
                    enableWebDebugging = enableWebDebugging,
                    suCompatStatus = suCompatStatus,
                    suCompatMode = suCompatMode,
                    isSuEnabled = isSuEnabled,
                    kernelUmountStatus = kernelUmountStatus,
                    isKernelUmountEnabled = isKernelUmountEnabled,
                    isDefaultUmountModules = isDefaultUmountModules,
                    isLkmMode = isLkmMode
                )
            }
            refreshSelinuxHide()
        }
    }

    fun setCheckUpdate(enabled: Boolean) {
        repo.checkUpdate = enabled
        _uiState.update { it.copy(checkUpdate = enabled) }
    }

    fun setUiMode(mode: String) {
        repo.uiMode = mode
        _uiState.update { it.copy(uiMode = mode) }
    }

    fun setCheckModuleUpdate(enabled: Boolean) {
        repo.checkModuleUpdate = enabled
        _uiState.update { it.copy(checkModuleUpdate = enabled) }
    }
    fun setAlternativeIcon(enabled: Boolean) {
        repo.alternativeIcon = enabled
        _uiState.update { it.copy(alternativeIcon = enabled) }
    }

    fun setThemeMode(mode: Int) {
        repo.themeMode = mode
        _uiState.update { it.copy(themeMode = mode) }
    }

    fun setKeyColor(color: Int) {
        repo.keyColor = color
        _uiState.update { it.copy(keyColor = color) }
    }

    fun setEnablePredictiveBack(enabled: Boolean) {
        repo.enablePredictiveBack = enabled
        _uiState.update { it.copy(enablePredictiveBack = enabled) }
    }

    fun setEnableBlur(enabled: Boolean) {
        repo.enableBlur = enabled
        _uiState.update { it.copy(enableBlur = enabled) }
    }

    fun setEnableFloatingBottomBar(enabled: Boolean) {
        repo.enableFloatingBottomBar = enabled
        _uiState.update { it.copy(enableFloatingBottomBar = enabled) }
    }

    fun setEnableFloatingBottomBarBlur(enabled: Boolean) {
        repo.enableFloatingBottomBarBlur = enabled
        _uiState.update { it.copy(enableFloatingBottomBarBlur = enabled) }
    }

    fun setPageScale(scale: Float) {
        repo.pageScale = scale
        _uiState.update { it.copy(pageScale = scale) }
    }

    fun setEnableWebDebugging(enabled: Boolean) {
        repo.enableWebDebugging = enabled
        _uiState.update { it.copy(enableWebDebugging = enabled) }
    }

    fun setSuCompatMode(mode: Int) {
        viewModelScope.launch(Dispatchers.IO) {
            when (mode) {
                0 -> if (repo.setSuEnabled(true)) {
                    repo.execKsudFeatureSave()
                    repo.setSuCompatModePref(0)
                    _uiState.update { it.copy(suCompatMode = 0, isSuEnabled = true) }
                }

                1 -> if (repo.setSuEnabled(true)) {
                    repo.execKsudFeatureSave()
                    if (repo.setSuEnabled(false)) {
                        // "Disable until reboot" implies it should be enabled on next boot.
                        // We set the preference to 0 (Enabled) to match the persistent state.
                        repo.setSuCompatModePref(0)
                        _uiState.update { it.copy(suCompatMode = 1, isSuEnabled = false) }
                    }
                }

                2 -> if (repo.setSuEnabled(false)) {
                    repo.execKsudFeatureSave()
                    repo.setSuCompatModePref(2)
                    _uiState.update { it.copy(suCompatMode = 2, isSuEnabled = false) }
                }
            }
        }
    }

    fun setKernelUmountEnabled(enabled: Boolean) {
        viewModelScope.launch(Dispatchers.IO) {
            if (repo.setKernelUmountEnabled(enabled)) {
                repo.execKsudFeatureSave()
                _uiState.update { it.copy(isKernelUmountEnabled = enabled) }
            }
        }
    }

    private suspend fun refreshSelinuxHide() {
        val active = repo.getSelinuxHideState()
        if (active < 0) {
            _uiState.update {
                it.copy(
                    selinuxHideStatus = if (active == -OsConstants.EOPNOTSUPP) "unsupported" else "error",
                    selinuxHideError = if (active == -OsConstants.EOPNOTSUPP) null else
                        ksuApp.getString(R.string.settings_selinux_hide_failed, active.toString())
                )
            }
            return
        }
        try {
            val status = repo.getSelinuxHideStatus()
            val requested = repo.getSelinuxHidePersistValue()?.let { it != 0L }
            _uiState.update {
                it.copy(
                    selinuxHideStatus = if (status == "supported" || status == "managed") status else "error",
                    isSelinuxHideEnabled = active != 0,
                    selinuxHideRequestedEnabled = requested,
                    selinuxHideError = if (status == "supported" || status == "managed") null else
                        ksuApp.getString(R.string.settings_selinux_hide_config_failed)
                )
            }
        } catch (error: Exception) {
            if (error is CancellationException) throw error
            _uiState.update {
                it.copy(
                    selinuxHideStatus = "error",
                    isSelinuxHideEnabled = active != 0,
                    selinuxHideError = ksuApp.getString(R.string.settings_selinux_hide_config_failed)
                )
            }
        }
    }

    fun setSelinuxHideEnabled(enabled: Boolean) {
        val state = _uiState.value
        if (state.selinuxHideBusy || state.selinuxHideStatus != "supported") return
        _uiState.update { it.copy(selinuxHideBusy = true, selinuxHideError = null) }
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val result = repo.setSelinuxHideEnabled(enabled)
                if (result != 0 && !(enabled && result == -OsConstants.EAGAIN)) {
                    _uiState.update {
                        it.copy(selinuxHideError = ksuApp.getString(
                            R.string.settings_selinux_hide_set_failed, result.toString()))
                    }
                    return@launch
                }
                // Persist the requested value explicitly, never snapshot the inactive value on EAGAIN.
                val saved = repo.persistSelinuxHide(enabled)
                refreshSelinuxHide()
                if (!saved) {
                    _uiState.update {
                        it.copy(selinuxHideError = ksuApp.getString(R.string.settings_selinux_hide_config_failed))
                    }
                } else if (_uiState.value.selinuxHidePendingReboot) {
                    withContext(Dispatchers.Main) {
                        Toast.makeText(ksuApp, R.string.settings_selinux_hide_reboot_required,
                            Toast.LENGTH_LONG).show()
                    }
                }
            } catch (error: Exception) {
                if (error is CancellationException) throw error
                refreshSelinuxHide()
                _uiState.update {
                    it.copy(selinuxHideError = ksuApp.getString(R.string.settings_selinux_hide_config_failed))
                }
            } finally {
                _uiState.update { it.copy(selinuxHideBusy = false) }
            }
        }
    }

    fun setDefaultUmountModules(enabled: Boolean) {
        viewModelScope.launch(Dispatchers.IO) {
            if (repo.setDefaultUmountModules(enabled)) {
                _uiState.update { it.copy(isDefaultUmountModules = enabled) }
            }
        }
    }
}
