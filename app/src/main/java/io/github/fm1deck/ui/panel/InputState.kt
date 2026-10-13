package io.github.fm1deck.ui.panel

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import io.github.fm1deck.engine.NativeBridge

/**
 * Tracks multi-touch panel input state and dispatches changes to NativeBridge.
 * Retained across P1 debug panel and P2 layout skins.
 */
class InputState {
    var buttonsMask by mutableIntStateOf(0)
        private set

    var keysMask by mutableIntStateOf(0)
        private set

    var masterAdc by mutableIntStateOf(700)
        private set

    // Maps pointer ID to the element currently pressed
    private val pointerTargets = mutableMapOf<Long, PointerTarget>()

    sealed class PointerTarget {
        data class Button(val index: Int) : PointerTarget()
        data class Key(val index: Int) : PointerTarget()
        data class Encoder(val role: Int, var lastY: Float) : PointerTarget()
        data class MasterSlider(var lastY: Float) : PointerTarget()
    }

    fun onPointerDown(pointerId: Long, target: PointerTarget) {
        pointerTargets[pointerId] = target
        recalculateMasks()
    }

    fun onPointerUp(pointerId: Long) {
        pointerTargets.remove(pointerId)
        recalculateMasks()
    }

    fun onPointerMove(pointerId: Long, y: Float, onMasterChange: ((Int) -> Unit)? = null) {
        when (val target = pointerTargets[pointerId]) {
            is PointerTarget.Encoder -> {
                val deltaY = target.lastY - y // drag up = positive/clockwise
                val stepThreshold = 20f
                if (kotlin.math.abs(deltaY) >= stepThreshold) {
                    val steps = (deltaY / stepThreshold).toInt()
                    if (steps != 0) {
                        NativeBridge.nativeEnc(target.role, steps)
                        target.lastY = y
                    }
                }
            }
            is PointerTarget.MasterSlider -> {
                val deltaY = target.lastY - y
                val stepThreshold = 3f
                if (kotlin.math.abs(deltaY) >= stepThreshold) {
                    val change = (deltaY * 3f).toInt()
                    val newAdc = (masterAdc + change).coerceIn(0, 1023)
                    if (newAdc != masterAdc) {
                        setMaster(newAdc)
                        onMasterChange?.invoke(newAdc)
                        target.lastY = y
                    }
                }
            }
            else -> {}
        }
    }

    fun setMaster(adc: Int) {
        val clamped = adc.coerceIn(0, 1023)
        masterAdc = clamped
        NativeBridge.nativeMaster(clamped)
    }

    fun setButtonHeld(index: Int, held: Boolean) {
        val newMask = if (held) {
            buttonsMask or (1 shl index)
        } else {
            buttonsMask and (1 shl index).inv()
        }
        if (newMask != buttonsMask) {
            buttonsMask = newMask
            NativeBridge.nativePanel(buttonsMask, keysMask)
        }
    }

    fun setKeyHeld(index: Int, held: Boolean) {
        val newMask = if (held) {
            keysMask or (1 shl index)
        } else {
            keysMask and (1 shl index).inv()
        }
        if (newMask != keysMask) {
            keysMask = newMask
            NativeBridge.nativePanel(buttonsMask, keysMask)
        }
    }

    private fun recalculateMasks() {
        var newButtons = 0
        var newKeys = 0
        for (target in pointerTargets.values) {
            when (target) {
                is PointerTarget.Button -> newButtons = newButtons or (1 shl target.index)
                is PointerTarget.Key -> newKeys = newKeys or (1 shl target.index)
                else -> {}
            }
        }
        if (newButtons != buttonsMask || newKeys != keysMask) {
            buttonsMask = newButtons
            keysMask = newKeys
            NativeBridge.nativePanel(buttonsMask, keysMask)
        }
    }
}
