// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026-present Open Handheld Collective
//   https://github.com/OpenHandheldCollective

package org.openhandheld.openabl.installer.abl

import android.content.Context
import java.io.File
import java.security.MessageDigest

data class Image(val version: String, val chip: String, val sha256: String, val bytes: Long)

// Release images the app knows by hash, from assets/known-images.txt.
class Known(val images: List<Image>) {
    val lengths get() = images.map { it.bytes }.toSortedSet()

    fun find(sha256: String, bytes: Long) = images.firstOrNull { it.sha256 == sha256 && it.bytes == bytes }

    companion object {
        fun parse(text: String) = Known(
            text.lineSequence().map { it.trim() }.filter { it.isNotEmpty() && !it.startsWith("#") }.map {
                val f = it.split(" ")
                Image(f[0], f[1], f[2], f[3].toLong())
            }.toList(),
        )
    }
}

// An image ready to hand to a root script.
class Ready(val version: String, val file: File, val sha256: String, val bytes: Long)

object Hash {
    fun sha256(file: File): String {
        val md = MessageDigest.getInstance("SHA-256")
        file.inputStream().use { s ->
            val buf = ByteArray(65536)
            while (true) {
                val n = s.read(buf)
                if (n < 0) break
                md.update(buf, 0, n)
            }
        }
        return md.digest().joinToString("") { "%02x".format(it) }
    }
}

// The images in the APK, and any newer release downloaded earlier.
class Images(private val context: Context, val bundled: String) {
    val known: Known = Known.parse(context.assets.open("known-images.txt").bufferedReader().use { it.readText() })
    private val root get() = File(context.getExternalFilesDir(null), "images").apply { mkdirs() }
    private val downloads get() = File(context.getExternalFilesDir(null), "releases").apply { mkdirs() }

    fun downloadedDir(version: String) = File(downloads, version)

    // Newest of the bundled and downloaded versions that has an image for the chip.
    fun newest(chip: String): Ready? {
        val name = "abl_signed-$chip.elf"
        val fetched = downloads.listFiles().orEmpty()
            .filter { File(it, name).isFile && File(it, "$name.sha256").isFile }
            .map { it.name }
        val best = (fetched + bundled).maxWithOrNull { a, b -> Versions.compare(a, b) } ?: return null
        return if (best == bundled) bundledImage(chip) else downloadedImage(best, chip)
    }

    private fun bundledImage(chip: String): Ready? {
        val name = "abl_signed-$chip.elf"
        val want = known.images.firstOrNull { it.version == bundled && it.chip == chip } ?: return null
        val out = File(File(root, bundled).apply { mkdirs() }, name)
        if (!out.isFile || Hash.sha256(out) != want.sha256) {
            context.assets.open("images/$name").use { i -> out.outputStream().use { i.copyTo(it) } }
        }
        val sha = Hash.sha256(out)
        return if (sha == want.sha256 && out.length() == want.bytes) Ready(bundled, out, sha, out.length()) else null
    }

    // Downloads were checked against the release's SHA256SUMS when they were saved.
    private fun downloadedImage(version: String, chip: String): Ready? {
        val name = "abl_signed-$chip.elf"
        val dir = downloadedDir(version)
        val file = File(dir, name)
        val want = File(dir, "$name.sha256").readText().trim().substringBefore(' ')
        val sha = Hash.sha256(file)
        return if (sha == want) Ready(version, file, sha, file.length()) else null
    }

    fun supports(chip: String) = bundledImage(chip) != null
}

object Versions {
    fun parse(v: String) = v.trim().removePrefix("v").split(".").map { it.toIntOrNull() ?: 0 }

    fun compare(a: String, b: String): Int {
        val x = parse(a)
        val y = parse(b)
        for (i in 0 until maxOf(x.size, y.size)) {
            val c = x.getOrElse(i) { 0 }.compareTo(y.getOrElse(i) { 0 })
            if (c != 0) return c
        }
        return 0
    }
}
