package regression
import android.os.Build
import com.sukisu.ultra.ksuApp
import com.sukisu.ultra.ui.util.exportPatchedImage
import java.nio.file.Files

fun exportRegression() {
    val dir = Files.createTempDirectory("export-regression-").toFile()
    try {
        val file = dir.resolve("patched.img").apply { writeBytes(byteArrayOf(1, 2, 3, 0)) }
        val resolver = ksuApp.contentResolver
        resolver.reset()
        check(exportPatchedImage(file).contains("Download/patched.img"))
        check(resolver.bytes.toByteArray().contentEquals(file.readBytes()))
        check(resolver.calls == listOf("insert", "open", "close", "publish"))
        for (failure in listOf("insert", "open", "write", "publish")) {
            resolver.reset(failure)
            check(runCatching { exportPatchedImage(file) }.isFailure)
            check(("delete" in resolver.calls) == (failure != "insert"))
            check("publish" !in resolver.calls || failure == "publish")
            check(file.exists())
        }
        resolver.reset()
        val empty = dir.resolve("empty.img").apply { writeBytes(byteArrayOf()) }
        check(runCatching { exportPatchedImage(empty) }.isFailure)
        check(resolver.calls.isEmpty())
        Build.VERSION.SDK_INT = 28
        check(runCatching { exportPatchedImage(file) }.isFailure)
        check(resolver.calls.isEmpty())
        println("PASS actual Downloads export: exact bytes, pending publication, 4 failure cleanup paths, empty/API28 refusal")
    } finally {
        Build.VERSION.SDK_INT = 35
        dir.deleteRecursively()
    }
}
