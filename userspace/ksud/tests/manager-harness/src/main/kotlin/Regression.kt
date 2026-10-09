package regression
import android.system.OsConstants
import com.sukisu.ultra.ksuApp
import com.sukisu.ultra.data.repository.SettingsRepositoryImpl
import com.sukisu.ultra.ui.viewmodel.SettingsViewModel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.setMain
import java.io.File

private suspend fun settled(model: SettingsViewModel) {
    withTimeout(5000) { while (model.uiState.value.selinuxHideBusy) delay(5) }
}
@OptIn(kotlinx.coroutines.ExperimentalCoroutinesApi::class)
fun main(args: Array<String>) = runBlocking {
    ksuApp.load(File(args.single()))
    Dispatchers.setMain(UnconfinedTestDispatcher())
    try {
        val fatal = SettingsRepositoryImpl().apply { setError = -38 }
        val model = SettingsViewModel(fatal)
        check(model.uiState.value.selinuxHideCanClearRequest)
        model.setSelinuxHideEnabled(true);settled(model)
        check(!model.uiState.value.isSelinuxHideEnabled)
        check(fatal.requestedStatus && fatal.saved == null)
        check(fatal.calls.toList() == listOf("set:true"))
        check(model.uiState.value.selinuxHideError!!.contains("Status hiding may remain"))
        check(model.uiState.value.selinuxHideError!!.contains("-38"))
        check(model.uiState.value.selinuxHideCanClearRequest)
        // Relaunch loses the error/pending preference; reset is still available from core state.
        val relaunched = SettingsViewModel(fatal)
        check(!relaunched.uiState.value.selinuxHidePendingReboot)
        check(relaunched.uiState.value.selinuxHideCanClearRequest)
        fatal.setError = 0
        relaunched.setSelinuxHideEnabled(false);settled(relaunched)
        check(!fatal.requestedStatus && !fatal.active && fatal.saved == 0L)
        check(fatal.calls.toList().takeLast(2) == listOf("set:false", "persist:false"))
        println("PASS fatal partial status -> relaunch -> explicit clear, no false success/persist")

        val pending = SettingsRepositoryImpl().apply { setError = -OsConstants.EAGAIN }
        val pendingModel = SettingsViewModel(pending)
        pendingModel.setSelinuxHideEnabled(true);settled(pendingModel)
        check(!pendingModel.uiState.value.isSelinuxHideEnabled)
        check(pendingModel.uiState.value.selinuxHidePendingReboot)
        check(pendingModel.uiState.value.selinuxHideError == null)
        check(pending.saved == 1L && pending.requestedStatus)
        pendingModel.setSelinuxHideEnabled(false);settled(pendingModel)
        check(!pending.requestedStatus && pending.saved == 0L)
        check(!pendingModel.uiState.value.selinuxHidePendingReboot)
        println("PASS EAGAIN persisted request and actual inactive -> cancellation")

        val unsupported = SettingsRepositoryImpl().apply { getError = -OsConstants.EOPNOTSUPP }
        val unsupportedModel = SettingsViewModel(unsupported)
        check(unsupportedModel.uiState.value.selinuxHideStatus == "unsupported")
        check(!unsupportedModel.uiState.value.selinuxHideCanClearRequest)
        unsupportedModel.setSelinuxHideEnabled(false)
        check(unsupported.calls.isEmpty())
        println("PASS old kernel unsupported -> no write/reset")

        val unreadable = SettingsRepositoryImpl().apply { getError = -5 }
        val unreadableModel = SettingsViewModel(unreadable)
        check(unreadableModel.uiState.value.selinuxHideStatus == "error")
        check(unreadableModel.uiState.value.selinuxHideError!!.contains("-5"))
        println("PASS native query error is distinct from unsupported/off")

        val persistence = SettingsRepositoryImpl().apply { persistThrows = true }
        val persistenceModel = SettingsViewModel(persistence)
        persistenceModel.setSelinuxHideEnabled(true);settled(persistenceModel)
        check(persistenceModel.uiState.value.isSelinuxHideEnabled)
        check(persistenceModel.uiState.value.selinuxHideError != null)
        check(persistence.saved == null)
        println("PASS persistence exception reports error while retaining actual activation")

        val managed = SettingsRepositoryImpl().apply { featureStatus = "managed" }
        val managedModel = SettingsViewModel(managed)
        check(!managedModel.uiState.value.selinuxHideCanClearRequest)
        managedModel.setSelinuxHideEnabled(true)
        check(managed.calls.isEmpty())
        println("PASS module-managed feature guard")
        exportRegression()
    } finally { Dispatchers.resetMain() }
}
