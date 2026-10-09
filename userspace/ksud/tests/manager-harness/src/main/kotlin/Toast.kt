package android.widget
import com.sukisu.ultra.FixtureApplication
class Toast {
    fun show() {}
    companion object {
        const val LENGTH_LONG = 1
        fun makeText(app: FixtureApplication, resource: Int, duration: Int): Toast {
            check(duration == LENGTH_LONG); app.getString(resource); return Toast()
        }
    }
}
