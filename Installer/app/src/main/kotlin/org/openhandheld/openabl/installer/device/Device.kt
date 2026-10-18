// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026-present Open Handheld Collective
//   https://github.com/OpenHandheldCollective

package org.openhandheld.openabl.installer.device

import android.os.Build

data class Device(
    val maker: String,
    val model: String,
    val fingerprint: String,
    val socModel: String,
    val platform: String,
    val chip: Chip?,
) {
    // The models OpenABL's panel table lists (Core/Panel.c). Android often
    // leaves the maker out or spells the name differently.
    val listed get() = listOf(model, "$maker $model").map(::squash).let { names ->
        LISTED.any { l -> squash(l).let { it in names || names.any { n -> n.length >= 6 && it.endsWith(n) } } }
    }

    companion object {
        private fun squash(s: String) = s.lowercase().filter { it.isLetterOrDigit() }

        private val LISTED = listOf(
            "MANGMI Pocket Max", "MANGMI Air Y Pro", "Retroid Pocket 5", "AYN Odin 2 Portal",
            "Retroid Pocket 6", "Retroid Pocket Nova", "KONKR Pocket FIT", "AYN Odin 3",
            "Retroid Pocket Classic Rev Q", "MANGMI Air X MQ65", "MANGMI Air X MQ66",
            "MANGMI Air X MQ66B", "MANGMI Air Y",
        )

        // [fake] stands in for the chip model when testing a debug build.
        fun read(fake: String? = null): Device {
            val soc = fake ?: prop("ro.soc.model")
            val platform = if (fake != null) "" else prop("ro.board.platform")
            return Device(Build.MANUFACTURER, Build.MODEL, Build.FINGERPRINT, soc, platform, Chip.identify(soc, platform))
        }

        private fun prop(key: String) = runCatching {
            val p = ProcessBuilder("getprop", key).redirectErrorStream(true).start()
            p.inputStream.bufferedReader().readText().trim().also { p.waitFor() }
        }.getOrDefault("")
    }
}
