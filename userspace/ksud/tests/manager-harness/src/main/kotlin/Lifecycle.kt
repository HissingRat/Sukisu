package androidx.lifecycle
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
open class ViewModel
private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
val ViewModel.viewModelScope: CoroutineScope get() = scope
