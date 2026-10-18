// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026-present Open Handheld Collective
//   https://github.com/OpenHandheldCollective

package org.openhandheld.openabl.installer.device

enum class Chip(val id: String, val soc: String, val models: List<String>, val platforms: List<String>) {
    SDM845("SDM845", "Snapdragon 845", listOf("SDM845"), listOf("sdm845")),
    SM4450("SM4450", "Snapdragon 4 Gen 2", listOf("SM4450"), emptyList()),
    SM6115("SM6115", "Snapdragon 662", listOf("SM6115", "QCS6125"), listOf("bengal")),
    SM6450("SM6450", "Snapdragon 6 Gen 1", listOf("SM6450"), emptyList()),
    SM7675("SM7675", "Snapdragon 7+ Gen 3", listOf("SM7675"), emptyList()),
    SM8250("SM8250", "Snapdragon 865", listOf("SM8250", "QCS8250"), listOf("kona")),
    SM8550("SM8550", "Snapdragon 8 Gen 2", listOf("SM8550", "QCS8550"), listOf("kalama")),
    SM8650("SM8650", "Snapdragon 8 Gen 3", listOf("SM8650", "QCS8650"), listOf("pineapple")),
    SM8750("SM8750", "Snapdragon 8 Elite", listOf("SM8750", "QCS8750", "SM7750", "QCS7790"), listOf("sun"));

    val label get() = "$soc ($id)"
    val short get() = "${soc.removePrefix("Snapdragon ")} ($id)"
    val image get() = "abl_signed-$id.elf"

    companion object {
        // Retroid reports the QCS name. The platform is used only when the model is empty.
        fun identify(socModel: String?, platform: String?): Chip? {
            val model = socModel.orEmpty().trim().uppercase()
            if (model.isNotEmpty() && model != "UNKNOWN") {
                return entries.firstOrNull { c -> c.models.any { model.startsWith(it) } }
            }
            val board = platform.orEmpty().trim().lowercase()
            return entries.firstOrNull { board in it.platforms }
        }
    }
}
