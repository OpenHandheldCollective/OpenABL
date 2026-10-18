// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026-present Open Handheld Collective
//   https://github.com/OpenHandheldCollective

package org.openhandheld.openabl.installer.abl

import org.openhandheld.openabl.installer.root.Status

// What the read-only job found.
data class Inspection(
    val active: String,
    val slotA: String,
    val slotB: String,
    val backups: List<String>,
) {
    companion object {
        fun from(status: Status, known: Known): Inspection {
            fun label(slot: String): String {
                val hits = known.lengths.mapNotNull { n ->
                    status.get("hash_${slot}_$n")?.let { known.find(it, n) }
                }
                return hits.firstOrNull()?.let { "OpenABL ${it.version}" } ?: "Not OpenABL"
            }
            return Inspection(
                status.get("active").orEmpty(),
                label("a"),
                label("b"),
                status.all("backup").sortedDescending(),
            )
        }
    }
}
