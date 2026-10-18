// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026-present Open Handheld Collective
//   https://github.com/OpenHandheldCollective

package org.openhandheld.openabl.installer.abl

import org.openhandheld.openabl.installer.device.Chip
import org.openhandheld.openabl.installer.root.Status
import java.io.File
import java.nio.file.Files
import java.security.MessageDigest
import java.util.Random

// A folder standing in for the handheld: plain files for abl_a and abl_b and
// for Download, and small stand-ins for the tools Android has and macOS lacks.
class FakeDevice(slotSize: Int = 65536) {
    val root: File = Files.createTempDirectory("openabl").toRealPath().toFile()
    val byName = File(root, "dev/block/by-name").apply { mkdirs() }
    val downloads = File(root, "Download").apply { mkdirs() }
    val appDir = File(root, "app").apply { mkdirs() }
    val status = File(appDir, "status")
    val slotA = File(byName, "abl_a")
    val slotB = File(byName, "abl_b")
    val stockA = bytes(slotSize, 1)
    val stockB = bytes(slotSize, 2)
    private val bin = File(root, "bin").apply { mkdirs() }
    private val props = File(root, "props")
    val env = Env(
        work = File(root, "work").path,
        downloads = downloads.path,
        byName = listOf(byName.path),
        blockDir = File(root, "dev/block").path,
        requireRoot = false,
        loose = true,
    )

    init {
        slotA.writeBytes(stockA)
        slotB.writeBytes(stockB)
        tool("sha256sum", "exec shasum -a 256 \"$@\"")
        tool("blockdev", "wc -c < \"$2\" | tr -d ' '")
        tool("getprop", "grep \"^$1=\" '${props.path}' | cut -d= -f2-")
        prop("ro.soc.model", "QCS8550")
        prop("ro.board.platform", "kalama")
        prop("ro.boot.slot_suffix", "_a")
    }

    private fun tool(name: String, body: String) {
        File(bin, name).apply { writeText("#!/bin/sh\n$body\n"); setExecutable(true) }
    }

    fun prop(key: String, value: String) {
        val lines = if (props.exists()) props.readLines().filter { !it.startsWith("$key=") } else emptyList()
        props.writeText((lines + "$key=$value").joinToString("\n") + "\n")
    }

    fun image(size: Int, elf: Boolean = true): File {
        val b = bytes(size, 7)
        if (elf) { b[0] = 0x7f; b[1] = 'E'.code.toByte(); b[2] = 'L'.code.toByte(); b[3] = 'F'.code.toByte() }
        return File(appDir, "image.elf").apply { writeBytes(b) }
    }

    // Runs the script as the app's root tool would: start it, then wait for its status.
    fun run(script: String): Status {
        val file = File(appDir, "job.sh").apply { writeText(script) }
        status.delete()
        val pb = ProcessBuilder("/bin/sh", file.path).redirectErrorStream(true)
        pb.environment()["PATH"] = bin.path + ":" + System.getenv("PATH")
        val p = pb.start()
        p.inputStream.readBytes()
        check(p.waitFor() == 0) { "the launching shell failed" }
        val end = System.currentTimeMillis() + 30_000
        while (System.currentTimeMillis() < end) {
            val s = runCatching { Status.parse(status.readText()) }.getOrNull()
            if (s != null && s.finished) return s
            Thread.sleep(50)
        }
        error("no result: ${runCatching { status.readText() }.getOrNull()}")
    }

    fun sha(b: ByteArray) = MessageDigest.getInstance("SHA-256").digest(b).joinToString("") { "%02x".format(it) }
    fun sha(f: File) = sha(f.readBytes())

    fun backups() = File(downloads, "OpenABL").listFiles().orEmpty().filter { it.name.startsWith("backup-") }

    fun install(image: File, sha: String = sha(image), stamp: String = "2026-10-18_190000", chip: Chip = Chip.SM8550) =
        run(Scripts.install(env, "job1", status.path, chip, stamp, "Model: Test One\nChip: SM8550", image.path, image.length(), sha))

    private fun bytes(n: Int, seed: Int) = ByteArray(n).also { Random(seed.toLong()).nextBytes(it) }
}
