package com.sukisu.ultra.ui.util

import android.content.ContentValues
import android.os.Build
import android.os.Environment
import android.provider.MediaStore
import com.sukisu.ultra.ksuApp
import java.io.File
import java.io.IOException

/** Publish only a completed image; no root shell or broad storage permission on Android 10+. */
internal fun exportPatchedImage(image: File): String {
    check(image.isFile && image.length() > 0) { "Patched image is missing or empty" }
    check(Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) { "Downloads API requires Android 10" }
    val resolver = ksuApp.contentResolver
    val values = ContentValues().apply {
        put(MediaStore.Downloads.DISPLAY_NAME, image.name)
        put(MediaStore.Downloads.MIME_TYPE, "application/octet-stream")
        put(MediaStore.Downloads.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS)
        put(MediaStore.Downloads.IS_PENDING, 1)
    }
    val uri = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values)
        ?: throw IOException("Cannot create image in Downloads")
    try {
        val stream = resolver.openOutputStream(uri) ?: throw IOException("Cannot open exported image")
        stream.use { output -> image.inputStream().use { it.copyTo(output) } }
        check(resolver.update(uri, ContentValues().apply {
            put(MediaStore.Downloads.IS_PENDING, 0)
        }, null, null) == 1) { "Cannot publish exported image" }
    } catch (e: Exception) {
        resolver.delete(uri, null, null)
        throw e
    }
    return "${Environment.DIRECTORY_DOWNLOADS}/${image.name} ($uri)"
}
