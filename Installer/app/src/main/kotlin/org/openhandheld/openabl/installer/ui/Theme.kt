// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026-present Open Handheld Collective
//   https://github.com/OpenHandheldCollective

package org.openhandheld.openabl.installer.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.ExperimentalTextApi
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontVariation
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.dp
import androidx.compose.foundation.layout.BoxWithConstraints
import org.openhandheld.openabl.installer.R

// The colours of the menu's theme (View/View.c).
object Colors {
    val Background = Color(0xFF000000)
    val Card = Color(0xFF171716)
    val Line = Color(0xFF2A2A28)
    val Text = Color(0xFFF1EFE8)
    val Text2 = Color(0xFFB4B2A9)
    val Text3 = Color(0xFF888780)
    val Accent = Color(0xFF5DCAA5)
    val AccentBg = Color(0xFF085041)
    val AccentText = Color(0xFFE1F5EE)
    val Danger = Color(0xFFF09595)
    val Brand = Color(0xFFFF5555)
    val Scrim = Color(0xD9000000)
}

@OptIn(ExperimentalTextApi::class)
val Montserrat = FontFamily(
    Font(R.font.montserrat, FontWeight.Normal, variationSettings = FontVariation.Settings(FontVariation.weight(400))),
    Font(R.font.montserrat, FontWeight.Medium, variationSettings = FontVariation.Settings(FontVariation.weight(500))),
)

// The menu is laid out in units, 327 of them from the top of the screen to
// the bottom, and the sizes here are its own (label 14.5, small text 12.5).
class Units(val u: Float, val density: Float) {
    fun dp(units: Float): Dp = (units * u).dp
    fun sp(units: Float): TextUnit = with(androidx.compose.ui.unit.Density(density)) { (units * u).dp.toSp() }
}

val LocalUnits = compositionLocalOf { Units(1f, 1f) }

@Composable
fun Themed(content: @Composable (landscape: Boolean) -> Unit) {
    BoxWithConstraints {
        val landscape = maxWidth > maxHeight
        val u = if (landscape) maxHeight.value / 327f else maxWidth.value / 330f
        CompositionLocalProvider(LocalUnits provides Units(u, LocalDensity.current.density)) {
            content(landscape)
        }
    }
}
