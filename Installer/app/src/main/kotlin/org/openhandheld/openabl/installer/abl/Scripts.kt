// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026-present Open Handheld Collective
//   https://github.com/OpenHandheldCollective

package org.openhandheld.openabl.installer.abl

import org.openhandheld.openabl.installer.device.Chip

// Where the scripts look. The defaults are the device's; tests point them at a folder.
data class Env(
    val work: String = "/data/local/tmp/openabl-installer",
    // Download as root sees it, under Android's storage layer.
    val downloads: String = "/data/media/0/Download",
    val byName: List<String> = listOf("/dev/block/by-name", "/dev/block/bootdevice/by-name"),
    val blockDir: String = "/dev/block",
    val requireRoot: Boolean = true,
    val loose: Boolean = false,
)

object Scripts {
    const val INSPECT = "inspect"
    const val BACKUP = "backup"
    const val INSTALL = "install"
    const val RESTORE = "restore"

    fun quote(s: String) = "'" + s.replace("'", "'\\''") + "'"

    private fun template(name: String): String =
        Scripts::class.java.getResourceAsStream("/scripts/$name.sh")!!.bufferedReader().use { it.readText() }

    private fun build(
        name: String,
        env: Env,
        job: String,
        status: String,
        chip: Chip?,
        vars: Map<String, String> = emptyMap(),
    ): String {
        val all = mapOf(
            "NAME" to name,
            "JOB" to quote(job),
            "WORK" to quote(env.work),
            "STATUS" to quote(status),
            "DOWNLOADS" to quote(env.downloads),
            "BYNAME" to quote(env.byName.joinToString(" ")),
            "BLOCKDIR" to quote(env.blockDir),
            "REQUIRE_ROOT" to if (env.requireRoot) "1" else "0",
            "LOOSE" to if (env.loose) "1" else "0",
            "MODELS" to (chip?.models.orEmpty().joinToString(" ")),
            "PLATFORMS" to (chip?.platforms.orEmpty().joinToString(" ")),
            "STAMP" to "",
            "INFO" to "''",
        ) + vars
        val body = listOf("prelude", "common", name).joinToString("") { template(it) }
        return Regex("@([A-Z_0-9]+)@").replace(body) { all[it.groupValues[1]] ?: error("no value for ${it.value}") }
    }

    fun inspect(env: Env, job: String, status: String, lengths: Collection<Long>) =
        build(INSPECT, env, job, status, null, mapOf("LENGTHS" to lengths.sorted().joinToString(" ")))

    fun backup(env: Env, job: String, status: String, stamp: String, info: String): String {
        check(Regex("[0-9]{4}-[0-9]{2}-[0-9]{2}_[0-9]{6}").matches(stamp)) { "bad stamp" }
        return build(BACKUP, env, job, status, null, mapOf("STAMP" to stamp, "INFO" to quote(info)))
    }

    fun install(
        env: Env, job: String, status: String, chip: Chip, stamp: String, info: String,
        image: String, length: Long, sha256: String,
    ): String {
        check(Regex("[0-9a-f]{64}").matches(sha256)) { "bad hash" }
        check(Regex("[0-9]{4}-[0-9]{2}-[0-9]{2}_[0-9]{6}").matches(stamp)) { "bad stamp" }
        return build(
            INSTALL, env, job, status, chip,
            mapOf("STAMP" to stamp, "INFO" to quote(info), "IMAGE" to quote(image), "LENGTH" to length.toString(), "SHA256" to sha256),
        )
    }

    fun restore(env: Env, job: String, status: String, model: String, folder: String): String {
        check(Regex("backup-[0-9_-]+").matches(folder)) { "bad folder" }
        return build(RESTORE, env, job, status, null, mapOf("FOLDER" to quote(folder), "MODEL" to quote(model)))
    }
}
