// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026-present Open Handheld Collective
//   https://github.com/OpenHandheldCollective

package org.openhandheld.openabl.installer.root

import android.content.ContentValues
import android.content.Context
import android.provider.MediaStore
import java.io.File
import java.util.UUID

sealed interface Launch {
    // The job is running and reports through its status file.
    data object Running : Launch
    // The user has to run the saved script as root, when the app can't.
    data class Script(val name: String) : Launch
    data class Failed(val why: String) : Launch
}

class Jobs(private val context: Context, val route: Route) {
    private val dir get() = File(context.getExternalFilesDir(null), "job").apply { mkdirs() }
    private val status get() = File(dir, "status")

    // The folder as root sees it, under Android's storage layer.
    fun rootPath(f: File) = f.absolutePath.replace(Regex("^/storage/emulated/0(?=/)"), "/data/media/0")

    val statusPath get() = rootPath(status)

    // A file the scripts leave next to the status file.
    fun left(name: String) = File(dir, name)

    fun newId() = UUID.randomUUID().toString()

    // Saves the script and starts it, or saves the one-line launcher for the user.
    fun launch(name: String, body: String): Launch {
        status.delete()
        val script = File(dir, "openabl-$name.sh")
        script.writeText(body)
        script.setReadable(true, false)
        val path = rootPath(script)
        if (!route.runs || !Root.start(route, path)) {
            return runCatching { Launch.Script(saveLauncher("sh $path\n")) }
                .getOrElse { Launch.Failed("Couldn't save the script to Download.") }
        }
        repeat(50) {
            if (status.exists()) return Launch.Running
            Thread.sleep(100)
        }
        return Launch.Failed("The script didn't report back.")
    }

    fun read(job: String): Status? =
        runCatching { Status.parse(status.readText()) }.getOrNull()?.takeIf { it.job == job }

    private fun saveLauncher(text: String): String {
        val name = "openabl-installer.sh"
        val resolver = context.contentResolver
        val downloads = MediaStore.Downloads.EXTERNAL_CONTENT_URI
        runCatching { resolver.delete(downloads, "${MediaStore.MediaColumns.DISPLAY_NAME} LIKE ?", arrayOf("openabl-installer%.sh")) }
        val values = ContentValues().apply {
            put(MediaStore.MediaColumns.DISPLAY_NAME, name)
            put(MediaStore.MediaColumns.MIME_TYPE, "application/x-sh")
            put(MediaStore.MediaColumns.RELATIVE_PATH, "Download/")
            put(MediaStore.MediaColumns.IS_PENDING, 1)
        }
        val uri = resolver.insert(downloads, values) ?: error("no uri")
        resolver.openOutputStream(uri)!!.use { it.write(text.toByteArray()) }
        resolver.update(uri, ContentValues().apply { put(MediaStore.MediaColumns.IS_PENDING, 0) }, null, null)
        return resolver.query(uri, arrayOf(MediaStore.MediaColumns.DISPLAY_NAME), null, null, null)?.use {
            if (it.moveToFirst()) it.getString(0) else null
        } ?: name
    }
}
