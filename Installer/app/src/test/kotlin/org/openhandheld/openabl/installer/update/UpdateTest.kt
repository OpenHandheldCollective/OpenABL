// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026-present Open Handheld Collective
//   https://github.com/OpenHandheldCollective

package org.openhandheld.openabl.installer.update

import org.openhandheld.openabl.installer.abl.Hash
import org.openhandheld.openabl.installer.abl.Known
import org.openhandheld.openabl.installer.abl.Versions
import java.io.File
import java.nio.file.Files
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class UpdateTest {
    private fun release(tag: String, assets: List<String>) = JSONObject(
        """{"tag_name":"$tag","assets":[${assets.joinToString(",") { """{"name":"$it","browser_download_url":"https://x/$it"}""" }}]}""",
    )

    @Test fun versionsCompareByNumber() {
        assertTrue(Versions.compare("v1.10", "1.9") > 0)
        assertEquals(0, Versions.compare("v1.0", "1.0"))
        assertTrue(Versions.compare("1.0", "1.1") < 0)
    }

    @Test fun onlyANewerReleaseIsOffered() {
        val names = listOf("openabl-v1.1.tar.gz", "SHA256SUMS")
        assertNull(Update.parse(release("v1.0", listOf("openabl-v1.0.tar.gz")), "1.0"))
        val r = Update.parse(release("v1.1", names), "1.0")
        assertNotNull(r)
        assertEquals("1.1", r!!.version)
        assertNotNull(r.sums)
    }

    @Test fun aReleaseWithoutSumsIsFoundButCantBeChecked() {
        val r = Update.parse(release("v1.1", listOf("openabl-v1.1.tar.gz")), "1.0")!!
        assertNull(r.sums)
    }

    @Test fun sumsAreParsed() {
        val m = Update.parseSums("${"a".repeat(64)}  openabl-v1.1.tar.gz\n${"b".repeat(64)} *abl_signed-SM8550.elf\nnot a line\n")
        assertEquals("a".repeat(64), m["openabl-v1.1.tar.gz"])
        assertEquals("b".repeat(64), m["abl_signed-SM8550.elf"])
        assertEquals(2, m.size)
    }

    @Test fun onlyImagesAreTakenFromTheArchive() {
        val dir = Files.createTempDirectory("tar").toRealPath().toFile()
        try {
            val pack = File(dir, "openabl-v1.1").apply { mkdirs() }
            File(pack, "SM8550").mkdirs()
            File(pack, "SM8550/abl_signed-SM8550.elf").writeText("image")
            File(pack, "SM8550/abl_signed-SM8550.elf.sha256").writeText("sum")
            File(pack, "SM8550/evil.elf").writeText("no")
            File(pack, "LICENSE").writeText("licence")
            val tgz = File(dir, "a.tar.gz")
            ProcessBuilder("tar", "-czf", tgz.path, "-C", dir.path, "openabl-v1.1").start().waitFor()
            val out = File(dir, "out").apply { mkdirs() }
            val files = Update.extract(tgz, out)
            assertEquals(listOf("abl_signed-SM8550.elf"), files.map { it.name })
            assertEquals("image", files[0].readText())
            assertEquals(1, out.listFiles()!!.size)
            assertEquals(Hash.sha256(files[0]), Hash.sha256(File(pack, "SM8550/abl_signed-SM8550.elf")))
        } finally {
            dir.deleteRecursively()
        }
    }

    @Test fun knownImagesAreParsedAndFound() {
        val k = Known.parse("# c\n1.0 SM8550 ${"a".repeat(64)} 532480\n1.0 SM8250 ${"b".repeat(64)} 258048\n")
        assertEquals(sortedSetOf(258048L, 532480L), k.lengths)
        assertEquals("SM8550", k.find("a".repeat(64), 532480)!!.chip)
        assertNull(k.find("a".repeat(64), 258048))
    }

    @Test fun newerInstallerOnlyWithItsApk() {
        assertNull(Update.parseApp(release("v1.1", listOf("openabl-v1.1.tar.gz")), "1.0"))
        assertNull(Update.parseApp(release("v1.0", listOf("openabl-installer-v1.0.apk")), "1.0"))
        assertEquals("1.1", Update.parseApp(release("v1.1", listOf("openabl-installer-v1.1.apk")), "1.0")!!.version)
    }
}
