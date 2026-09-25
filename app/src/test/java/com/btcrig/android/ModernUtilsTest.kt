package com.btcrig.android

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ModernUtilsTest {
    @Test
    fun updateComparisonHandlesTagsAndPatchVersions() {
        assertTrue(updateStateFor("0.3.0", "v0.3.1", "url").available)
        assertFalse(updateStateFor("0.3.1", "v0.3.1", "url").available)
        assertFalse(updateStateFor("0.3.1", "v0.3", "url").available)
    }

    @Test
    fun logAndHashrateTextStayReadable() {
        assertEquals("error", cleanLog("\u001B[31merror\u001B[0m"))
        assertEquals("52.47 MH/s", formatHashrate(52_470_000.0))
    }
}
