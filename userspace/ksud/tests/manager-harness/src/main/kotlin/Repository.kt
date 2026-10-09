package com.sukisu.ultra.data.repository
import java.util.concurrent.ConcurrentLinkedQueue
// External Android/JNI/shell services are fixtures; production ViewModel/state/interface are copied by Gradle.
open class SettingsRepositoryImpl : SettingsRepository {
    override var uiMode = "MD3"
    override var checkUpdate = true
    override var checkModuleUpdate = true
    override var alternativeIcon = false
    override var themeMode = 0
    override var keyColor = 0
    override var enablePredictiveBack = false
    override var enableBlur = false
    override var enableFloatingBottomBar = false
    override var enableFloatingBottomBarBlur = false
    override var pageScale = 1f
    override var enableWebDebugging = false
    override suspend fun getSuCompatStatus() = "supported"
    override suspend fun getSuCompatPersistValue(): Long? = null
    override fun isSuEnabled() = true
    override fun setSuEnabled(enabled: Boolean) = true
    override fun setSuCompatModePref(mode: Int) {}
    override fun getSuCompatModePref() = 0
    override suspend fun getKernelUmountStatus() = "supported"
    override fun isKernelUmountEnabled() = false
    override fun setKernelUmountEnabled(enabled: Boolean) = true
    override fun isDefaultUmountModules() = false
    override fun setDefaultUmountModules(enabled: Boolean) = true
    override fun isLkmMode() = true
    override fun execKsudFeatureSave() {}
    @Volatile var active = false
    @Volatile var requestedStatus = false
    @Volatile var saved: Long? = null
    @Volatile var setError = 0
    @Volatile var getError = 0
    @Volatile var featureStatus = "supported"
    @Volatile var configReadFails = false
    @Volatile var persistThrows = false
    @Volatile var persistSucceeds = true
    val calls = ConcurrentLinkedQueue<String>()
    override suspend fun getSelinuxHideStatus() = featureStatus
    override suspend fun getSelinuxHidePersistValue(): Long? {
        check(!configReadFails) { "Unreadable config" };return saved
    }
    override fun getSelinuxHideState(): Int = if (getError != 0) getError else if (active) 1 else 0
    override fun setSelinuxHideEnabled(enabled: Boolean): Int {
        calls.add("set:$enabled")
        // Upstream parity: a failed enable can already request status-only hiding.
        requestedStatus = enabled
        if (enabled && setError != 0) return setError
        active = enabled;return 0
    }
    override fun persistSelinuxHide(enabled: Boolean): Boolean {
        calls.add("persist:$enabled")
        check(!persistThrows) { "Root shell unavailable" }
        if (persistSucceeds) saved = if (enabled) 1 else 0
        return persistSucceeds
    }
}
