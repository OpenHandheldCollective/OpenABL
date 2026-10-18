// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026-present Open Handheld Collective
//   https://github.com/OpenHandheldCollective

package org.openhandheld.openabl.installer.abl

import org.openhandheld.openabl.installer.device.Chip
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ScriptsTest {
    private val env = Env()

    @Test fun placeholdersAreAllFilled() {
        val all = listOf(
            Scripts.inspect(env, "j", "/s", listOf(1, 2)),
            Scripts.backup(env, "j", "/s", "2026-10-18_190000", "Model: X"),
            Scripts.install(env, "j", "/s", Chip.SM8550, "2026-10-18_190000", "Model: X", "/i", 5, "a".repeat(64)),
            Scripts.restore(env, "j", "/s", "X", "backup-2026-10-18_190000"),
        )
        all.forEach { assertFalse(Regex("@[A-Z_0-9]+@").containsMatchIn(it)) }
    }

    @Test fun valuesAreQuoted() {
        assertEquals("'it'\\''s'", Scripts.quote("it's"))
        val s = Scripts.backup(env, "j", "/s", "2026-10-18_190000", "Model: it's")
        assertTrue(s.contains("'Model: it'\\''s'"))
    }

    @Test fun chipListsReachTheScript() {
        val s = Scripts.install(env, "j", "/s", Chip.SM8550, "2026-10-18_190000", "", "/i", 5, "a".repeat(64))
        assertTrue(s.contains("for x in SM8550 QCS8550;"))
        assertTrue(s.contains("for x in kalama;"))
    }

    @Test(expected = IllegalStateException::class)
    fun badFolderIsRefused() {
        Scripts.restore(env, "j", "/s", "X", "../backup-1")
    }

    @Test(expected = IllegalStateException::class)
    fun badHashIsRefused() {
        Scripts.install(env, "j", "/s", Chip.SM8550, "2026-10-18_190000", "", "/i", 5, "zz")
    }
}
