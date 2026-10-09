package com.sukisu.ultra.data.repository

interface SettingsRepository {
    var uiMode: String
    var checkUpdate: Boolean
    var checkModuleUpdate: Boolean
    var alternativeIcon : Boolean
    var themeMode: Int
    var keyColor: Int
    var enablePredictiveBack: Boolean
    var enableBlur: Boolean
    var enableFloatingBottomBar: Boolean
    var enableFloatingBottomBarBlur: Boolean
    var pageScale: Float
    var enableWebDebugging: Boolean

    suspend fun getSuCompatStatus(): String
    suspend fun getSuCompatPersistValue(): Long?
    fun isSuEnabled(): Boolean
    fun setSuEnabled(enabled: Boolean): Boolean
    fun setSuCompatModePref(mode: Int)
    fun getSuCompatModePref(): Int

    suspend fun getKernelUmountStatus(): String
    fun isKernelUmountEnabled(): Boolean
    fun setKernelUmountEnabled(enabled: Boolean): Boolean

    suspend fun getSelinuxHideStatus(): String
    suspend fun getSelinuxHidePersistValue(): Long?
    fun getSelinuxHideState(): Int
    fun setSelinuxHideEnabled(enabled: Boolean): Int
    fun persistSelinuxHide(enabled: Boolean): Boolean

    fun isDefaultUmountModules(): Boolean
    fun setDefaultUmountModules(enabled: Boolean): Boolean

    fun isLkmMode(): Boolean

    fun execKsudFeatureSave()
}
