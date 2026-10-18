// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026-present Open Handheld Collective
//   https://github.com/OpenHandheldCollective

package org.openhandheld.openabl.installer

import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import org.openhandheld.openabl.installer.abl.Env
import org.openhandheld.openabl.installer.abl.Images
import org.openhandheld.openabl.installer.abl.Inspection
import org.openhandheld.openabl.installer.abl.Marker
import org.openhandheld.openabl.installer.abl.Ready
import org.openhandheld.openabl.installer.abl.Scripts
import org.openhandheld.openabl.installer.device.Chip
import org.openhandheld.openabl.installer.device.Device
import org.openhandheld.openabl.installer.root.Jobs
import org.openhandheld.openabl.installer.root.Launch
import org.openhandheld.openabl.installer.root.Root
import org.openhandheld.openabl.installer.root.Route
import org.openhandheld.openabl.installer.root.Status
import org.openhandheld.openabl.installer.update.AppRelease
import org.openhandheld.openabl.installer.update.Release
import org.openhandheld.openabl.installer.update.Update
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

sealed interface Screen {
    data object Home : Screen
    data object Restore : Screen
    data object Licenses : Screen
    data object Job : Screen
}

class Confirm(
    val title: String, val text: String, val action: String,
    val other: String? = null, val runOther: () -> Unit = {},
    val run: () -> Unit,
)

// A job on screen: what it is doing, or how it ended.
class Running(val title: String, val message: String, val script: String? = null, val result: Status? = null, val keys: Boolean = false)

sealed interface Updates {
    data object Idle : Updates
    data object Checking : Updates
    data object Current : Updates
    data object Offline : Updates
    class Found(val release: Release) : Updates
    data object Downloading : Updates
    class Failed(val why: String) : Updates
}

class Installer(private val context: Context, private val scope: CoroutineScope, val device: Device = Device.read()) {
    val bundled: String = context.packageManager.getPackageInfo(context.packageName, 0).versionName.orEmpty()
    val images = Images(context, bundled)
    private val env = Env()

    var route by mutableStateOf<Route?>(null)
    var inspection by mutableStateOf<Inspection?>(null)
    var inspectNote by mutableStateOf<String?>(null)
    var reading by mutableStateOf(false)
    var updates by mutableStateOf<Updates>(Updates.Idle)
    var screen by mutableStateOf<Screen>(Screen.Home)
    var confirm by mutableStateOf<Confirm?>(null)
    var running by mutableStateOf<Running?>(null)
    var ready by mutableStateOf<Ready?>(null)
    var app by mutableStateOf<AppRelease?>(null)

    private var jobs: Jobs? = null
    private var busy = false
    private var gen = 0

    val chip get() = device.chip
    val auto get() = route == Route.Xsu || route == Route.PServer

    fun start() {
        scope.launch {
            val found = withContext(Dispatchers.IO) { Root.find() }
            jobs = Jobs(context, found)
            refreshReady()
            route = found
            if (auto && chip != null) inspect()
            checkUpdates()
        }
    }

    private suspend fun refreshReady() {
        val c = chip ?: return
        ready = withContext(Dispatchers.IO) { images.newest(c.id) }
    }

    fun back() {
        when {
            confirm != null -> confirm = null
            screen == Screen.Job -> if (running?.result != null || running?.script != null) leaveJob()
            screen != Screen.Home -> screen = Screen.Home
        }
    }

    // Reads the slots. Needs root, so on the script routes the user starts it.
    fun inspect() {
        val j = jobs ?: return
        if (busy || chip == null) return
        reading = true
        inspectNote = null
        scope.launch {
            val id = j.newId()
            val body = Scripts.inspect(env, id, j.statusPath, images.known.lengths)
            val launched = withContext(Dispatchers.IO) { j.launch(Scripts.INSPECT, body) }
            when (launched) {
                is Launch.Failed -> { inspectNote = launched.why; reading = false }
                is Launch.Script -> {
                    running = Running("Read the slots", "Run the script in Download as root.", launched.name)
                    screen = Screen.Job
                    val s = await(j, id)
                    if (s != null) finishInspect(s)
                }
                Launch.Running -> {
                    val s = await(j, id)
                    if (s != null) finishInspect(s)
                }
            }
            reading = false
        }
    }

    private fun finishInspect(s: Status) {
        val j = jobs
        if (s.done) {
            inspection = Inspection.from(s, images.known) { slot -> j?.left("slot_$slot.img")?.let { Marker.version(it) } }
        } else inspectNote = s.message
        if (screen == Screen.Job && running?.script != null) leaveJob()
    }

    private suspend fun await(j: Jobs, id: String, onStatus: (Status) -> Unit = {}): Status? {
        var last = ""
        val mine = gen
        while (true) {
            val s = withContext(Dispatchers.IO) { j.read(id) }
            if (s != null) {
                if (s.message != last) { last = s.message; onStatus(s) }
                if (s.finished) return s
            }
            if (mine != gen) return null
            delay(300)
        }
    }

    private fun leaveJob() {
        gen++
        running = null
        screen = Screen.Home
    }

    fun askInstall() {
        val r = ready ?: return
        val c = chip ?: return
        val listed = if (device.listed) "" else " This model isn't one OpenABL lists. Go on only if it has abl_a and abl_b."
        confirm = Confirm(
            "Install OpenABL ${r.version}",
            "Backs up both slots to Download/OpenABL, then writes OpenABL (${c.image}) to abl_a and abl_b.$listed",
            "Install",
        ) { install(r, c) }
    }

    private fun install(r: Ready, c: Chip) {
        runJob("Installing", "install") { j, id ->
            Scripts.install(env, id, j.statusPath, c, stamp(), info(), j.rootPath(r.file), r.bytes, r.sha256)
        }
    }

    fun askRestore(name: String) {
        confirm = Confirm(
            pretty(name), "Restore checks the backup, then writes it to abl_a and abl_b. Delete removes it from Download/OpenABL.", "Restore",
            "Delete", { askDelete(name) },
        ) { runJob("Restoring", "restore") { j, id -> Scripts.restore(env, id, j.statusPath, device.model, name) } }
    }

    private fun askDelete(name: String) {
        confirm = Confirm("Delete ${pretty(name)}", "The backup is gone for good after this.", "Delete") {
            runJob("Deleting", "delete") { j, id -> Scripts.delete(env, id, j.statusPath, name) }
        }
    }

    fun askReboot() {
        confirm = Confirm("Reboot to bootloader", "Restarts the handheld into the bootloader, OpenABL's menu once it's installed.", "Reboot") {
            runJob("Rebooting", "reboot") { j, id -> Scripts.reboot(env, id, j.statusPath) }
        }
    }

    private fun runJob(title: String, name: String, build: (Jobs, String) -> String) {
        val j = jobs ?: return
        if (busy || reading) return
        busy = true
        confirm = null
        running = Running(title, "Starting")
        screen = Screen.Job
        scope.launch {
            val id = j.newId()
            val launched = withContext(Dispatchers.IO) { j.launch(name, build(j, id)) }
            when (launched) {
                is Launch.Failed -> running = Running(title, launched.why, result = Status(id, "failed", launched.why, emptyList()))
                is Launch.Script -> running = Running(title, "Run the script in Download as root.", launched.name)
                Launch.Running -> {}
            }
            if (launched !is Launch.Failed) {
                val s = await(j, id) { s -> if (running?.result == null) running = Running(title, s.message, running?.script) }
                if (s != null) {
                    running = Running(title, s.message, result = s, keys = s.done && name != "delete" && name != "reboot")
                    afterJob(name, s)
                }
            }
            busy = false
        }
    }

    private fun afterJob(name: String, s: Status) {
        val cur = inspection
        s.get("backup")?.let { b -> inspection = (cur ?: Inspection("", "", "", emptyList())).let { it.copy(backups = (listOf(b) + it.backups).distinct()) } }
        if (s.done && name == "install") {
            val label = "OpenABL ${ready?.version}"
            inspection = (inspection ?: Inspection("", "", "", emptyList())).copy(slotA = label, slotB = label)
        }
        if (s.done && name == "restore") inspection = null
        if (s.done && name == "delete") s.get("deleted")?.let { gone ->
            inspection = inspection?.let { it.copy(backups = it.backups - gone) }
        }
    }

    fun finish() = leaveJob()

    fun checkUpdates() {
        if (updates == Updates.Checking || updates == Updates.Downloading) return
        updates = Updates.Checking
        scope.launch {
            updates = try {
                val (r, a) = withContext(Dispatchers.IO) { Update.check(bundledOrNewer(), bundled) }
                app = a
                if (r == null) Updates.Current else Updates.Found(r)
            } catch (e: Exception) {
                Updates.Offline
            }
        }
    }

    private fun bundledOrNewer() = ready?.version ?: bundled

    fun download(r: Release) {
        updates = Updates.Downloading
        scope.launch {
            updates = try {
                withContext(Dispatchers.IO) {
                    val tmp = File(context.cacheDir, "release.tar.gz")
                    Update.download(r, images.downloadedDir(r.version), tmp)
                }
                refreshReady()
                Updates.Current
            } catch (e: Exception) {
                Updates.Failed(e.message ?: "The download failed.")
            }
        }
    }

    private fun stamp() = SimpleDateFormat("yyyy-MM-dd_HHmmss", Locale.US).format(Date())

    private fun info() = listOf(
        "Model: ${device.model}",
        "Maker: ${device.maker}",
        "Chip: ${chip?.id}",
        "Fingerprint: ${device.fingerprint}",
        "Date: ${SimpleDateFormat("yyyy-MM-dd HH:mm:ss Z", Locale.US).format(Date())}",
    ).joinToString("\n")

    companion object {
        fun pretty(backup: String) = backup.removePrefix("backup-").replace('_', ' ').let {
            if (it.length == 17) "${it.substring(0, 13)}:${it.substring(13, 15)}:${it.substring(15)}" else it
        }
    }
}
