// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026-present Open Handheld Collective
//   https://github.com/OpenHandheldCollective

package org.openhandheld.openabl.installer.abl

import org.tukaani.xz.LZMAInputStream
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File

// "OpenABL\0<version>\0" in the LZMA-packed firmware volume, in every build.
object Marker {
    private val NAME = "OpenABL\u0000".toByteArray()
    private const val MAX_OUT = 4 shl 20

    fun version(file: File): String? = runCatching { if (file.isFile) version(file.readBytes()) else null }.getOrNull()

    fun version(image: ByteArray): String? {
        // An LZMA header: properties 0x5D and a power-of-two dictionary.
        for (i in 0 until minOf(image.size - 13, 65536)) {
            if (image[i] != 0x5D.toByte() || image[i + 1] != 0.toByte()) continue
            val dict = (image[i + 1].toInt() and 0xFF) or ((image[i + 2].toInt() and 0xFF) shl 8) or
                ((image[i + 3].toInt() and 0xFF) shl 16) or ((image[i + 4].toInt() and 0xFF) shl 24)
            if (dict == 0 || dict and (dict - 1) != 0) continue
            val out = unpack(image, i) ?: continue
            return find(out)
        }
        return null
    }

    private fun unpack(image: ByteArray, at: Int): ByteArray? = runCatching {
        val out = ByteArrayOutputStream()
        LZMAInputStream(ByteArrayInputStream(image, at, image.size - at)).use { s ->
            val buf = ByteArray(65536)
            while (out.size() < MAX_OUT) {
                val n = s.read(buf)
                if (n < 0) break
                out.write(buf, 0, n)
            }
        }
        out.toByteArray().takeIf { it.size > 65536 }
    }.getOrNull()

    fun find(data: ByteArray): String? {
        var i = 0
        while (true) {
            i = indexOf(data, NAME, i)
            if (i < 0) return null
            val start = i + NAME.size
            var end = start
            while (end < data.size && end - start < 16 && data[end] != 0.toByte()) end++
            val v = String(data, start, end - start, Charsets.US_ASCII)
            if (Regex("[0-9]+\\.[0-9]+").matches(v)) return v
            i = start
        }
    }

    private fun indexOf(data: ByteArray, what: ByteArray, from: Int): Int {
        outer@ for (i in from..data.size - what.size) {
            for (k in what.indices) if (data[i + k] != what[k]) continue@outer
            return i
        }
        return -1
    }
}
