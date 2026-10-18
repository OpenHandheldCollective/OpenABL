// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026-present Open Handheld Collective
//   https://github.com/OpenHandheldCollective

package org.openhandheld.openabl.installer.ui

import androidx.compose.foundation.Canvas
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.withTransform
import androidx.compose.ui.graphics.vector.PathParser
import androidx.compose.ui.unit.Dp
import androidx.compose.runtime.Composable
import androidx.compose.foundation.layout.size

// Outline icons on a 24 by 24 grid, drawn after Tabler Icons (MIT).
enum class Icon(val path: String) {
    Chip("M6 5h12a1 1 0 0 1 1 1v12a1 1 0 0 1-1 1H6a1 1 0 0 1-1-1V6a1 1 0 0 1 1-1z M9 9h6v6H9z M9 3v2 M15 3v2 M9 19v2 M15 19v2 M3 9h2 M3 15h2 M19 9h2 M19 15h2"),
    Gamepad("M6 8h12a4 4 0 0 1 4 4a4 4 0 0 1-4 4H6a4 4 0 0 1-4-4a4 4 0 0 1 4-4z M7 12h4 M9 10v4 M16 11v.01 M18 13v.01"),
    Key("M8 15a4 4 0 1 0 0-8a4 4 0 0 0 0 8z M11 11h10 M18 11v3 M21 11v2"),
    Database("M4 6a8 3 0 1 0 16 0a8 3 0 1 0-16 0 M4 6v6a8 3 0 0 0 16 0V6 M4 12v6a8 3 0 0 0 16 0v-6"),
    Layers("M12 4l8 4l-8 4l-8-4z M4 12l8 4l8-4 M4 16l8 4l8-4"),
    Download("M4 17v2a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-2 M7 11l5 5l5-5 M12 4v12"),
    Refresh("M20 11a8.1 8.1 0 0 0-15.5-2M4 5v4h4 M4 13a8.1 8.1 0 0 0 15.5 2M20 19v-4h-4"),
    Trash("M4 7h16 M10 11v6 M14 11v6 M5 7l1 12a2 2 0 0 0 2 2h8a2 2 0 0 0 2-2l1-12 M9 7V4a1 1 0 0 1 1-1h4a1 1 0 0 1 1 1v3"),
    History("M12 8v4l2 2 M3.05 11a9 9 0 1 1 .5 4 M3 20v-5h5"),
    File("M14 3v4a1 1 0 0 0 1 1h4 M17 21H7a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h7l5 5v11a2 2 0 0 1-2 2z M9 13h6 M9 17h6"),
    Alert("M12 9v4 M12 17v.01 M10.3 4.2L2.8 17a2 2 0 0 0 1.7 3h15a2 2 0 0 0 1.7-3L13.7 4.2a2 2 0 0 0-3.4 0z"),
    Check("M5 12l5 5L20 7"),
    Exit("M14 8V6a2 2 0 0 0-2-2H5a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h7a2 2 0 0 0 2-2v-2 M9 12h12 M18 9l3 3l-3 3"),
    Chevron("M9 6l6 6l-6 6"),
    Down("M6 9l6 6l6-6"),
    Search("M10 10m-7 0a7 7 0 1 0 14 0a7 7 0 1 0-14 0 M21 21l-6-6"),
    UpDown("M12 3v18 M9 6l3-3l3 3 M9 18l3 3l3-3"),
    Enter("M5 12h14 M13 6l6 6l-6 6"),
    Back("M9 14L4 9l5-5 M4 9h10.5a5.5 5.5 0 0 1 0 11H11");

    val parsed: Path by lazy { PathParser().parsePathString(path).toPath() }
}

@Composable
fun Glyph(icon: Icon, size: Dp, color: Color, modifier: Modifier = Modifier) {
    Canvas(modifier.size(size)) {
        val scale = this.size.width / 24f
        withTransform({ scale(scale, scale, pivot = Offset.Zero) }) {
            drawPath(icon.parsed, color, style = Stroke(1.75f, cap = StrokeCap.Round, join = StrokeJoin.Round))
        }
    }
}
