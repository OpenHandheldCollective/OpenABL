// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026-present Open Handheld Collective
//   https://github.com/OpenHandheldCollective

package org.openhandheld.openabl.installer.root

// A job's status file: key=value lines, some keys more than once.
class Status(val job: String, val state: String, val message: String, private val pairs: List<Pair<String, String>>) {
    val done get() = state == "done"
    val failed get() = state == "failed"
    val finished get() = done || failed

    fun get(key: String) = pairs.firstOrNull { it.first == key }?.second
    fun all(key: String) = pairs.filter { it.first == key }.map { it.second }

    companion object {
        fun parse(text: String): Status? {
            val pairs = text.lines().mapNotNull { l -> l.split('=', limit = 2).takeIf { it.size == 2 }?.let { it[0] to it[1] } }
            val get = { k: String -> pairs.firstOrNull { it.first == k }?.second }
            val job = get("job") ?: return null
            return Status(job, get("state").orEmpty(), get("message").orEmpty(), pairs)
        }
    }
}
