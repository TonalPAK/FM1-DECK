package io.github.fm1deck

import org.junit.Assert.assertTrue
import org.junit.Test

class SmokeTest {
    @Test
    fun testRevisionPositive() {
        assertTrue(APP_EDIT_REVISION > 0.0)
    }
}
