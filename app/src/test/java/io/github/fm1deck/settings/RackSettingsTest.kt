package io.github.fm1deck.settings

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

class RackSettingsTest {

    @get:Rule
    val tempFolder = TemporaryFolder()

    @Test
    fun oldSettingsBecomeSlotOne() {
        val s = SettingsStore.fromJson("""{ "schema": 1, "activeFwId": "felucca", "masterAdc": 850 }""")
        assertEquals(listOf("felucca", "", ""), s.rack)
        assertEquals(0, s.rackFocus)
        assertEquals(850, s.masterAdc)
        assertEquals("felucca", s.activeFwId)
    }

    @Test
    fun aFirmwareRunsInOneSlotOnly() {
        val s = AppSettings(rack = listOf("jiant", "felucca", "")).normalized()
            .withSlot(2, "jiant")
        assertEquals(listOf("", "felucca", "jiant"), s.rack)
    }

    @Test
    fun focusNeverRestsOnAnEmptySlot() {
        val s = AppSettings(rack = listOf("jiant", "felucca", ""), rackFocus = 1).normalized().withSlot(1, "")
        assertEquals(0, s.rackFocus)
        assertEquals("jiant", s.activeFwId)
    }

    @Test
    fun aNewFirmwareInASlotStartsHeard() {
        val s = AppSettings(rack = listOf("jiant", "felucca", ""), rackMute = 0b10, rackSolo = 0b10).normalized()
            .withSlot(1, "hortator")
        assertEquals(0, s.rackMute)
        assertEquals(0, s.rackSolo)
    }

    @Test
    fun rackRoundTrips() {
        val file = File(tempFolder.root, "settings.json")
        val original = AppSettings(
            rack = listOf("hortator", "felucca", "jiant"), rackMasters = listOf(600, 700, 800), rackFocus = 2,
            rackTempo = 128.0, rackMute = 1, rackSolo = 4, keyLight = "violet", keyBeatMarks = false
        ).normalized()
        SettingsStore.save(file, original)
        val loaded = SettingsStore.load(file)
        assertEquals(original.rack, loaded.rack)
        assertEquals(original.rackMasters, loaded.rackMasters)
        assertEquals(2, loaded.rackFocus)
        assertEquals(800, loaded.masterAdc)
        assertEquals(128.0, loaded.rackTempo, 0.0)
        assertEquals(1, loaded.rackMute)
        assertEquals(4, loaded.rackSolo)
        assertEquals("violet", loaded.keyLight)
        assertEquals(false, loaded.keyBeatMarks)
    }

    @Test
    fun sessionsSaveListAndName() {
        val store = SessionStore(tempFolder.root)
        assertEquals("SESSION 01", store.nextName())
        val s = AppSettings(rack = listOf("hortator", "jiant", ""), rackFocus = 1, masterAdc = 900).normalized()
        val session = Session.of(store.newId("Live A", 1000L), "Live A", s.copy(masterAdc = 900), 1000L)
        store.writeMeta(session)
        val listed = store.list()
        assertEquals(1, listed.size)
        assertEquals("Live A", listed[0].name)
        assertEquals(listOf("hortator", "jiant", ""), listed[0].rack)
        assertEquals(900, listed[0].rackMasters[1])
        assertEquals(listed[0].id, store.findByName("live a")?.id)
        val back = listed[0].applyTo(AppSettings())
        assertEquals(1, back.rackFocus)
        assertEquals("jiant", back.activeFwId)
        store.delete(listed[0].id)
        assertNull(store.findByName("Live A"))
    }
}
