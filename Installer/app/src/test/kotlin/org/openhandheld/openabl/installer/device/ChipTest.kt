// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026-present Open Handheld Collective
//   https://github.com/OpenHandheldCollective

package org.openhandheld.openabl.installer.device

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class ChipTest {
    @Test fun modelNames() {
        assertEquals(Chip.SM8550, Chip.identify("SM8550", ""))
        assertEquals(Chip.SM8550, Chip.identify("QCS8550", "kalama"))
        assertEquals(Chip.SM8550, Chip.identify(" sm8550p ", null))
        assertEquals(Chip.SM8250, Chip.identify("SM8250-AC", ""))
        assertEquals(Chip.SM8750, Chip.identify("SM8750", ""))
        assertEquals(Chip.SDM845, Chip.identify("SDM845", ""))
    }

    @Test fun platformOnlyWhenModelIsMissing() {
        assertEquals(Chip.SM8650, Chip.identify("", "pineapple"))
        assertEquals(Chip.SM8650, Chip.identify("unknown", "PINEAPPLE"))
        assertEquals(Chip.SM8250, Chip.identify(null, "kona"))
        assertNull(Chip.identify("SM8350", "pineapple"))
    }

    @Test fun unsupported() {
        assertNull(Chip.identify("SM8350", "lahaina"))
        assertNull(Chip.identify("", ""))
        assertNull(Chip.identify(null, null))
    }

    @Test fun everyChipHasAnImageName() {
        assertEquals(9, Chip.entries.size)
        assertEquals("abl_signed-SM8550.elf", Chip.SM8550.image)
    }
}
