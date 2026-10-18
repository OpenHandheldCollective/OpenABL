// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026-present Open Handheld Collective
//   https://github.com/OpenHandheldCollective

package org.openhandheld.openabl.installer.update

import org.openhandheld.openabl.installer.abl.Hash
import org.openhandheld.openabl.installer.abl.Versions
import org.json.JSONObject
import java.io.File
import java.io.InputStream
import java.net.HttpURLConnection
import java.net.URL
import java.util.zip.GZIPInputStream

class Release(val version: String, val archive: Asset, val sums: Asset?) {
    class Asset(val name: String, val url: String, val digest: String?)
}

// A newer installer APK, opened in the browser.
class AppRelease(val version: String, val page: String)

class UpdateException(message: String) : Exception(message)

object Update {
    private const val LATEST = "https://api.github.com/repos/OpenHandheldCollective/OpenABL/releases/latest"
    private const val MAX_SUMS = 64 * 1024
    private const val MAX_ARCHIVE = 64L * 1024 * 1024

    // The latest release's images if newer than [images], and its APK if newer than [app].
    fun check(images: String, app: String): Pair<Release?, AppRelease?> {
        val json = JSONObject(String(fetch(LATEST, 1 shl 20)))
        return parse(json, images) to parseApp(json, app)
    }

    fun parseApp(json: JSONObject, have: String): AppRelease? {
        val tag = json.getString("tag_name")
        if (Versions.compare(tag, have) <= 0) return null
        val assets = json.getJSONArray("assets")
        val apk = (0 until assets.length()).map { assets.getJSONObject(it) }
            .firstOrNull { it.getString("name") == "openabl-installer-$tag.apk" } ?: return null
        return AppRelease(tag.removePrefix("v"), json.optString("html_url").ifEmpty { apk.getString("browser_download_url") })
    }

    fun parse(json: JSONObject, have: String): Release? {
        val tag = json.getString("tag_name")
        if (Versions.compare(tag, have) <= 0) return null
        val assets = json.getJSONArray("assets")
        val list = (0 until assets.length()).map { assets.getJSONObject(it) }.map {
            Release.Asset(it.getString("name"), it.getString("browser_download_url"), it.optString("digest").takeIf { d -> d.isNotEmpty() })
        }
        val archive = list.firstOrNull { it.name == "openabl-$tag.tar.gz" } ?: return null
        return Release(tag.removePrefix("v"), archive, list.firstOrNull { it.name == "SHA256SUMS" })
    }

    // Downloads and checks a release into [dest] (abl_signed-*.elf and their .sha256).
    // Nothing is kept unless every image is listed in the release's SHA256SUMS and matches.
    fun download(release: Release, dest: File, tmp: File) {
        val sums = release.sums ?: throw UpdateException("This release has no SHA256SUMS, so it can't be checked.")
        val listed = parseSums(String(fetch(sums.url, MAX_SUMS)))
        val want = listed[release.archive.name] ?: throw UpdateException("SHA256SUMS doesn't list ${release.archive.name}.")
        val api = release.archive.digest?.removePrefix("sha256:")
        if (api != null && api != want) throw UpdateException("GitHub and SHA256SUMS disagree about the download.")

        tmp.parentFile?.mkdirs()
        open(release.archive.url).use { s -> tmp.outputStream().use { copyLimited(s, it, MAX_ARCHIVE) } }
        if (Hash.sha256(tmp) != want) {
            tmp.delete()
            throw UpdateException("The download doesn't match SHA256SUMS.")
        }

        val out = File(dest.path + ".part")
        out.deleteRecursively()
        out.mkdirs()
        try {
            val images = extract(tmp, out)
            if (images.isEmpty()) throw UpdateException("The archive has no images.")
            for (f in images) {
                val sha = listed[f.name] ?: throw UpdateException("SHA256SUMS doesn't list ${f.name}.")
                if (Hash.sha256(f) != sha) throw UpdateException("${f.name} doesn't match SHA256SUMS.")
                File(out, "${f.name}.sha256").writeText("$sha  ${f.name}\n")
            }
            dest.deleteRecursively()
            if (!out.renameTo(dest)) throw UpdateException("Couldn't save the download.")
        } finally {
            out.deleteRecursively()
            tmp.delete()
        }
    }

    fun parseSums(text: String): Map<String, String> = text.lineSequence().mapNotNull {
        val m = Regex("^([0-9a-f]{64}) [ *](\\S+)$").find(it.trim()) ?: return@mapNotNull null
        m.groupValues[2] to m.groupValues[1]
    }.toMap()

    // Only the image files of a .tar.gz, by their names; paths in the archive are ignored.
    fun extract(archive: File, into: File): List<File> {
        val found = ArrayList<File>()
        GZIPInputStream(archive.inputStream().buffered()).use { s ->
            val head = ByteArray(512)
            while (readFully(s, head)) {
                if (head.all { it == 0.toByte() }) break
                val path = String(head, 0, 100).trimEnd('\u0000')
                val size = String(head, 124, 12).trim('\u0000', ' ').toLongOrNull(8) ?: throw UpdateException("The archive is damaged.")
                val type = head[156].toInt().toChar()
                val name = path.substringAfterLast('/')
                val wanted = (type == '0' || type == '\u0000') && Regex("abl_signed-[A-Z0-9]+\\.elf").matches(name)
                if (wanted) {
                    if (size > 4L * 1024 * 1024) throw UpdateException("An image in the archive is too large.")
                    val f = File(into, name)
                    f.outputStream().use { copyLimited(s, it, size, exact = true) }
                    found += f
                } else {
                    skip(s, size)
                }
                skip(s, (512 - size % 512) % 512)
            }
        }
        return found
    }

    private fun readFully(s: InputStream, b: ByteArray): Boolean {
        var n = 0
        while (n < b.size) {
            val r = s.read(b, n, b.size - n)
            if (r < 0) return false
            n += r
        }
        return true
    }

    private fun skip(s: InputStream, n: Long) {
        var left = n
        val buf = ByteArray(8192)
        while (left > 0) {
            val r = s.read(buf, 0, minOf(left, buf.size.toLong()).toInt())
            if (r < 0) throw UpdateException("The archive is damaged.")
            left -= r
        }
    }

    private fun copyLimited(i: InputStream, o: java.io.OutputStream, limit: Long, exact: Boolean = false) {
        val buf = ByteArray(65536)
        var total = 0L
        while (true) {
            val want = if (exact) minOf(buf.size.toLong(), limit - total).toInt() else buf.size
            if (want == 0) break
            val n = i.read(buf, 0, want)
            if (n < 0) {
                if (exact) throw UpdateException("The archive is damaged.")
                break
            }
            total += n
            if (total > limit) throw UpdateException("The download is too large.")
            o.write(buf, 0, n)
        }
    }

    private fun open(url: String): InputStream {
        val c = URL(url).openConnection() as HttpURLConnection
        c.connectTimeout = 10000
        c.readTimeout = 20000
        c.setRequestProperty("Accept", "application/vnd.github+json")
        c.setRequestProperty("User-Agent", "OpenABL-Installer")
        if (c.responseCode != 200) throw UpdateException("The server answered ${c.responseCode}.")
        return c.inputStream
    }

    private fun fetch(url: String, max: Int): ByteArray = open(url).use {
        val out = java.io.ByteArrayOutputStream()
        copyLimited(it, out, max.toLong())
        out.toByteArray()
    }
}
