package org.openhandheld.openabl.installer.abl

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assume.assumeTrue
import org.junit.Test
import java.io.File

class MarkerTest {
    @Test fun findsTheVersionAfterTheName() {
        assertEquals("1.2", Marker.find("xx\u0000OpenABL\u0000\u0000OpenABL\u00001.2\u0000yy".toByteArray()))
    }

    @Test fun nothingInOtherImages() {
        assertNull(Marker.version(ByteArray(200_000) { (it * 31).toByte() }))
    }

    // The release image fetched for the APK, when it is there.
    @Test fun readsTheReleaseImage() {
        val f = File("../.cache/unpacked/openabl-v1.0/SM8650/abl_signed-SM8650.elf")
        assumeTrue(f.isFile)
        assertEquals("1.0", Marker.version(f))
    }
}
