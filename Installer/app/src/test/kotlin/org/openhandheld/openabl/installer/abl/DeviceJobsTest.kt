// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026-present Open Handheld Collective
//   https://github.com/OpenHandheldCollective

package org.openhandheld.openabl.installer.abl

import org.openhandheld.openabl.installer.device.Chip
import java.io.File
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

class DeviceJobsTest {
    private lateinit var d: FakeDevice

    @Before fun setUp() { d = FakeDevice() }
    @After fun tearDown() { d.root.deleteRecursively() }

    private fun untouched() {
        assertArrayEquals(d.stockA, d.slotA.readBytes())
        assertArrayEquals(d.stockB, d.slotB.readBytes())
    }

    @Test fun inspectHashesTheKnownLengths() {
        File(d.downloads, "OpenABL/backup-2026-01-01_000000").apply { mkdirs() }.let { File(it, "SHA256SUMS").writeText("x") }
        val s = d.run(Scripts.inspect(d.env, "j", d.status.path, listOf(1000L, 4096L)))
        assertTrue(s.message, s.done)
        assertEquals(d.sha(d.stockA.copyOf(1000)), s.get("hash_a_1000"))
        assertEquals(d.sha(d.stockB.copyOf(4096)), s.get("hash_b_4096"))
        assertEquals("65536", s.get("size_a"))
        assertEquals("_a", s.get("active"))
        assertEquals(listOf("backup-2026-01-01_000000"), s.all("backup"))
    }

    @Test fun inspectNeedsBothSlots() {
        d.slotB.delete()
        val s = d.run(Scripts.inspect(d.env, "j", d.status.path, listOf(1000L)))
        assertTrue(s.failed)
    }

    @Test fun backupSavesAndChecks() {
        val s = d.run(Scripts.backup(d.env, "j", d.status.path, "2026-10-18_190000", "Model: Test One\nDate: now"))
        assertTrue(s.message, s.done)
        val dir = File(d.downloads, "OpenABL/backup-2026-10-18_190000")
        assertArrayEquals(d.stockA, File(dir, "abl_a.img").readBytes())
        assertArrayEquals(d.stockB, File(dir, "abl_b.img").readBytes())
        val sums = File(dir, "SHA256SUMS").readLines()
        assertEquals("${d.sha(d.stockA)}  abl_a.img", sums[0])
        assertEquals("${d.sha(d.stockB)}  abl_b.img", sums[1])
        assertEquals(listOf("Model: Test One", "Date: now"), File(dir, "info.txt").readLines())
        assertEquals("backup-2026-10-18_190000", s.get("backup"))
        untouched()
    }

    @Test fun installWritesBothSlotsAfterTheBackup() {
        val img = d.image(12345)
        val s = d.install(img)
        assertTrue(s.message, s.done)
        assertEquals("ok", s.get("slot_a"))
        assertEquals("ok", s.get("slot_b"))
        for ((slot, stock) in listOf(d.slotA to d.stockA, d.slotB to d.stockB)) {
            val now = slot.readBytes()
            assertEquals(stock.size, now.size)
            assertArrayEquals(img.readBytes(), now.copyOf(12345))
            assertArrayEquals(stock.copyOfRange(12345, stock.size), now.copyOfRange(12345, now.size))
        }
        val dir = d.backups().single()
        assertArrayEquals(d.stockA, File(dir, "abl_a.img").readBytes())
        assertArrayEquals(d.stockB, File(dir, "abl_b.img").readBytes())
    }

    @Test fun installTakesTheImageSizeFromItsLength() {
        val s = d.install(d.image(4096))
        assertTrue(s.message, s.done)
        assertArrayEquals(d.image(4096).readBytes(), d.slotA.readBytes().copyOf(4096))
    }

    @Test fun installRefusesWhenTheBackupCantBeMade() {
        File(d.downloads, "OpenABL/backup-2026-10-18_190000").mkdirs()
        val s = d.install(d.image(12345))
        assertTrue(s.failed)
        untouched()
    }

    @Test fun installRefusesAWrongHash() {
        val s = d.install(d.image(12345), sha = "0".repeat(64))
        assertTrue(s.failed)
        assertTrue(s.message, s.message.contains("hash"))
        untouched()
        assertTrue(d.backups().isEmpty())
    }

    @Test fun installRefusesAnImageLargerThanThePartition() {
        val s = d.install(d.image(65536 + 4096))
        assertTrue(s.failed)
        assertTrue(s.message, s.message.contains("larger"))
        untouched()
    }

    @Test fun installRefusesSomethingThatIsNotAnElf() {
        val s = d.install(d.image(12345, elf = false))
        assertTrue(s.failed)
        untouched()
    }

    @Test fun installRefusesAnotherChip() {
        d.prop("ro.soc.model", "SM8650")
        d.prop("ro.board.platform", "pineapple")
        val s = d.install(d.image(12345))
        assertTrue(s.failed)
        untouched()
    }

    @Test fun installRefusesOnePartitionTwice() {
        d.slotB.delete()
        java.nio.file.Files.createSymbolicLink(d.slotB.toPath(), d.slotA.toPath())
        val s = d.install(d.image(12345))
        assertTrue(s.failed)
        assertTrue(s.message, s.message.contains("one partition"))
    }

    @Test fun installRefusesAPartitionOutsideTheBlockDirectory() {
        val other = File(d.root, "elsewhere").apply { writeBytes(d.stockB) }
        d.slotB.delete()
        java.nio.file.Files.createSymbolicLink(d.slotB.toPath(), other.toPath())
        val s = d.install(d.image(12345))
        assertTrue(s.failed)
        assertArrayEquals(d.stockB, other.readBytes())
    }

    @Test fun installPutsTheBackupBackWhenASlotWontTakeTheImage() {
        d.slotB.setWritable(false)
        val s = d.install(d.image(12345))
        assertTrue(s.failed)
        assertEquals("failed", s.get("slot_b"))
        assertTrue(s.message, s.message.contains("abl_b"))
        assertArrayEquals(d.stockA, d.slotA.readBytes())
    }

    @Test fun installWritesTheSlotItBootsFromLast() {
        d.prop("ro.boot.slot_suffix", "_a")
        d.slotB.setWritable(false)
        d.install(d.image(12345))
        assertArrayEquals("slot a was written before b failed", d.stockA, d.slotA.readBytes())
    }

    @Test fun restoreWritesTheBackupBack() {
        d.install(d.image(12345))
        val name = d.backups().single().name
        val s = d.run(Scripts.restore(d.env, "j2", d.status.path, "Test One", name))
        assertTrue(s.message, s.done)
        untouched()
    }

    @Test fun restoreRefusesATamperedBackup() {
        d.install(d.image(12345))
        val dir = d.backups().single()
        File(dir, "abl_a.img").apply { writeBytes(readBytes().also { it[10] = (it[10] + 1).toByte() }) }
        val s = d.run(Scripts.restore(d.env, "j2", d.status.path, "Test One", dir.name))
        assertTrue(s.failed)
        assertTrue(s.message, s.message.contains("SHA256SUMS"))
        assertFalse(d.slotA.readBytes().contentEquals(d.stockA))
    }

    @Test fun restoreRefusesAnotherHandheldsBackup() {
        d.install(d.image(12345))
        val dir = d.backups().single()
        val s = d.run(Scripts.restore(d.env, "j2", d.status.path, "Other Model", dir.name))
        assertTrue(s.failed)
        assertTrue(s.message, s.message.contains("another handheld"))
    }

    @Test fun restoreRefusesAnIncompleteBackup() {
        d.install(d.image(12345))
        val dir = d.backups().single()
        File(dir, "abl_b.img").delete()
        val s = d.run(Scripts.restore(d.env, "j2", d.status.path, "Test One", dir.name))
        assertTrue(s.failed)
    }

    @Test fun backupFolderMustExist() {
        val s = d.run(Scripts.restore(d.env, "j2", d.status.path, "Test One", "backup-2026-01-01_000000"))
        assertTrue(s.failed)
    }

    @Test fun theScriptCopiesItselfAndLeavesNothingBehind() {
        d.run(Scripts.backup(d.env, "j", d.status.path, "2026-10-18_190000", "Model: x"))
        Thread.sleep(300)
        assertTrue(File(d.env.work).listFiles().orEmpty().none { it.name.endsWith(".sh") || it.name.endsWith(".extra") })
        assertFalse(File(d.appDir, "status.tmp").exists())
    }

    @Test fun everyChipListedHasModelsToCheck() {
        Chip.entries.forEach { assertTrue(it.models.isNotEmpty()) }
    }
}
