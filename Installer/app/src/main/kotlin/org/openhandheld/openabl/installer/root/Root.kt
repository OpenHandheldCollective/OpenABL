// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026-present Open Handheld Collective
//   https://github.com/OpenHandheldCollective

package org.openhandheld.openabl.installer.root

import android.os.Build
import android.os.IBinder
import android.os.Parcel
import java.io.File
import java.util.concurrent.TimeUnit
import org.openhandheld.openabl.installer.abl.Scripts

// How a script gets root, found by looking. Nothing is run to find out, except
// "id" through Retroid's service, which any app may call.
enum class Route(val label: String, val runs: Boolean) {
    Xsu("xsu", true),
    PServer("Retroid service", true),
    Su("su", true),
    Launcher("Run script as root", false),
    None("None found", false),
}

object Root {
    const val XSU = "/product/bin/xsu"
    private const val SERVICE = "PServerBinder"
    private val SU_DIRS = listOf("/system/bin", "/system/xbin", "/sbin", "/vendor/bin", "/product/bin", "/debug_ramdisk")

    fun find(): Route = when {
        File(XSU).exists() -> Route.Xsu
        serviceBinder() != null -> if (serviceRun("id -u")?.trim() == "0") Route.PServer else Route.Launcher
        hasSu() -> Route.Su
        Build.MANUFACTURER.equals("Moorechip", ignoreCase = true) -> Route.Launcher
        else -> Route.None
    }

    private fun hasSu(): Boolean {
        val path = System.getenv("PATH").orEmpty().split(':')
        return (SU_DIRS + path).any { it.isNotEmpty() && File(it, "su").exists() }
    }

    // Starts the script as root. It detaches itself, so this returns at once.
    fun start(route: Route, script: String): Boolean = when (route) {
        Route.Xsu -> exec(listOf(XSU, "sh", script))
        Route.PServer -> serviceRun("sh ${Scripts.quote(script)}") != null
        Route.Su -> exec(listOf("su", "-c", "sh ${Scripts.quote(script)}"))
        else -> false
    }

    private fun exec(cmd: List<String>) = runCatching {
        val p = ProcessBuilder(cmd).redirectErrorStream(true).start()
        p.inputStream.readBytes()
        p.waitFor(20, TimeUnit.SECONDS) && p.exitValue() == 0
    }.getOrDefault(false)

    // The service has no interface name, so "service call" can't reach it. Code 0
    // takes [command, "1"] and replies with the output.
    private fun serviceBinder(): IBinder? = runCatching {
        Class.forName("android.os.ServiceManager").getMethod("getService", String::class.java)
            .invoke(null, SERVICE) as IBinder?
    }.getOrNull()?.takeIf { it.isBinderAlive }

    private fun serviceRun(command: String): String? {
        val binder = serviceBinder() ?: return null
        val data = Parcel.obtain()
        val reply = Parcel.obtain()
        return try {
            data.writeStringArray(arrayOf(command, "1"))
            if (binder.transact(0, data, reply, 0)) String(reply.createByteArray() ?: ByteArray(0)) else null
        } catch (e: Exception) {
            null
        } finally {
            data.recycle()
            reply.recycle()
        }
    }
}
