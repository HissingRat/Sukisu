package android.content

import java.io.ByteArrayOutputStream
import java.io.IOException
import java.io.OutputStream

class ContentValues {
    val values = mutableMapOf<String, Any>()
    fun put(key: String, value: String) { values[key] = value }
    fun put(key: String, value: Int) { values[key] = value }
}

class ContentResolver {
    val bytes = ByteArrayOutputStream()
    val calls = mutableListOf<String>()
    var fail = ""
    fun reset(failure: String = "") { bytes.reset(); calls.clear(); fail = failure }
    fun insert(uri: String, values: ContentValues): String? {
        check(values.values["pending"] == 1)
        check(values.values["path"] == "Download")
        calls += "insert"
        return if (fail == "insert") null else "$uri/42"
    }
    fun openOutputStream(uri: String): OutputStream? {
        check(uri.endsWith("/42")); calls += "open"
        if (fail == "open") return null
        return object : OutputStream() {
            override fun write(b: Int) {
                if (fail == "write") throw IOException("fixture write error")
                bytes.write(b)
            }
            override fun close() { calls += "close" }
        }
    }
    fun update(uri: String, values: ContentValues, selection: String?, args: Array<String>?): Int {
        check(uri.endsWith("/42") && selection == null && args == null)
        check(values.values["pending"] == 0)
        calls += "publish"
        return if (fail == "publish") 0 else 1
    }
    fun delete(uri: String, selection: String?, args: Array<String>?): Int {
        check(uri.endsWith("/42") && selection == null && args == null)
        calls += "delete"; return 1
    }
}
