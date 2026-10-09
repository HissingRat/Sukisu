package com.sukisu.ultra
import java.io.File
import java.util.Locale
import javax.xml.parsers.DocumentBuilderFactory
object R {
    object string {
        const val settings_selinux_hide_failed = 1
        const val settings_selinux_hide_config_failed = 2
        const val settings_selinux_hide_reboot_required = 3
        const val settings_selinux_hide_set_failed = 4
    }
}
class FixtureApplication {
    val contentResolver = android.content.ContentResolver()
    private val names = mapOf(1 to "settings_selinux_hide_failed", 2 to "settings_selinux_hide_config_failed",
        3 to "settings_selinux_hide_reboot_required", 4 to "settings_selinux_hide_set_failed")
    private var values = emptyMap<String, String>()
    fun load(file: File) {
        val entries = DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(file).getElementsByTagName("string")
        values = (0 until entries.length).associate { index ->
            val entry = entries.item(index)
            entry.attributes.getNamedItem("name").nodeValue to entry.textContent
        }
    }
    fun getString(id: Int, vararg arguments: Any): String =
        String.format(Locale.ROOT, checkNotNull(values[names[id]]), *arguments)
}
val ksuApp = FixtureApplication()
