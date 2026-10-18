// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026-present Open Handheld Collective
//   https://github.com/OpenHandheldCollective

package org.openhandheld.openabl.installer.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.focusable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.onKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.input.pointer.pointerInput

import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.foundation.text.BasicText

@Composable
fun Txt(
    text: String,
    size: Float = 14.5f,
    color: Color = Colors.Text,
    weight: FontWeight = FontWeight.Normal,
    modifier: Modifier = Modifier,
    align: TextAlign = TextAlign.Start,
    lines: Int = Int.MAX_VALUE,
) {
    val un = LocalUnits.current
    BasicText(
        text,
        modifier,
        TextStyle(color = color, fontSize = un.sp(size), fontFamily = Montserrat, fontWeight = weight, textAlign = align, lineHeight = un.sp(size * 1.35f)),
        maxLines = lines,
        overflow = TextOverflow.Ellipsis,
    )
}

@Composable
fun Header(version: String, newer: String?, checking: Boolean, onClick: () -> Unit) {
    val un = LocalUnits.current
    Row(Modifier.fillMaxWidth().height(un.dp(44f)), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            val mark = buildAnnotatedString {
                withStyle(SpanStyle(color = Colors.Text)) { append("Open") }
                withStyle(SpanStyle(color = Colors.Brand)) { append("ABL") }
                withStyle(SpanStyle(color = Colors.Text2)) { append(" Installer") }
            }
            BasicText(
                mark,
                style = TextStyle(fontSize = un.sp(20f), fontFamily = Montserrat, fontWeight = FontWeight.Medium),
            )
        }
        // The app's version; checks for a newer installer, or opens it.
        var focused by remember { mutableStateOf(false) }
        val shape = RoundedCornerShape(un.dp(12f))
        val edge = if (focused) Colors.Accent else if (newer != null) Colors.Brand else Colors.Line
        Row(
            Modifier.height(un.dp(24f)).clip(shape).background(if (focused) Colors.AccentBg else Color.Transparent)
                .border(BorderStroke(un.dp(if (focused) 1.5f else 1f), edge), shape)
                .onFocusChanged { focused = it.isFocused }
                .onKeyEvent {
                    if (it.key in selectKeys) {
                        if (it.type == KeyEventType.KeyUp) onClick()
                        true
                    } else false
                }
                .pointerInput(onClick) { detectTapGestures { onClick() } }.focusable()
                .padding(horizontal = un.dp(10f)),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            val fg = if (focused) Colors.AccentText else if (newer != null) Colors.Brand else Colors.Text2
            Txt(if (newer != null) "v$newer available" else "v$version", 12.5f, fg)
            if (newer == null) {
                Spacer(Modifier.width(un.dp(6f)))
                Glyph(Icon.Refresh, un.dp(12f), if (checking) Colors.Text3 else fg)
            }
        }
    }
}

class Hint(val label: String, val icon: Icon? = null, val key: String? = null)

@Composable
fun Footer(hints: List<Hint>, modifier: Modifier = Modifier) {
    val un = LocalUnits.current
    Column(modifier.fillMaxWidth()) {
        Box(Modifier.fillMaxWidth().height(un.dp(1f)).background(Colors.Line))
        Row(Modifier.fillMaxWidth().weight(1f), Arrangement.Center, Alignment.CenterVertically) {
            hints.forEachIndexed { i, h ->
                if (i > 0) Spacer(Modifier.width(un.dp(18f)))
                if (h.icon != null) Glyph(h.icon, un.dp(13f), Colors.Text2)
                if (h.key != null) {
                    Box(Modifier.size(un.dp(13f)).border(BorderStroke(un.dp(1f), Colors.Text2), CircleShape), contentAlignment = Alignment.Center) {
                        Txt(h.key, 8f, Colors.Text2, align = TextAlign.Center)
                    }
                }
                Spacer(Modifier.width(un.dp(6f)))
                Txt(h.label, 12.5f, Colors.Text3)
            }
        }
    }
}

class Fact(val icon: Icon, val label: String, val value: String, val muted: Boolean = false)

@Composable
fun SystemPanel(facts: List<Fact>, modifier: Modifier = Modifier) {
    val un = LocalUnits.current
    Column(modifier.clip(RoundedCornerShape(un.dp(11f))).background(Colors.Card).padding(un.dp(11f))) {
        Txt("System", 12.5f, Colors.Text3, FontWeight.Medium, Modifier.padding(bottom = un.dp(3f)))
        facts.forEach {
            Row(Modifier.fillMaxWidth().height(un.dp(27f)), verticalAlignment = Alignment.CenterVertically) {
                Glyph(it.icon, un.dp(15f), Colors.Text3)
                Spacer(Modifier.width(un.dp(10f)))
                Txt(it.label, 12.5f, Colors.Text2, modifier = Modifier.width(un.dp(44f)), lines = 1)
                Spacer(Modifier.width(un.dp(8f)))
                Txt(it.value, 12.5f, if (it.muted) Colors.Text3 else Colors.Text, modifier = Modifier.weight(1f), align = TextAlign.End, lines = 1)
            }
        }
    }
}

@Composable
fun Section(text: String, first: Boolean = false) {
    val un = LocalUnits.current
    Txt(text, 12.5f, Colors.Text3, FontWeight.Medium, Modifier.padding(start = un.dp(10f), top = un.dp(if (first) 0f else 6f), bottom = un.dp(2f)))
}

@Composable
fun Note(text: String, color: Color = Colors.Text3) {
    val un = LocalUnits.current
    Txt(text, 12.5f, color, modifier = Modifier.padding(horizontal = un.dp(10f), vertical = un.dp(4f)))
}

enum class Tone { Normal, Danger }

private val selectKeys = setOf(Key.Enter, Key.NumPadEnter, Key.DirectionCenter, Key.ButtonA)

@Composable
fun Entry(
    icon: Icon,
    label: String,
    modifier: Modifier = Modifier,
    value: String? = null,
    chevron: Boolean = false,
    enabled: Boolean = true,
    tone: Tone = Tone.Normal,
    requester: FocusRequester? = null,
    onFocus: () -> Unit = {},
    onClick: () -> Unit,
) {
    val un = LocalUnits.current
    var focused by remember { mutableStateOf(false) }
    val selected = focused && enabled
    val fg = when {
        !enabled -> Colors.Text3
        selected -> Colors.AccentText
        tone == Tone.Danger -> Colors.Danger
        else -> Colors.Text
    }
    val ic = if (selected) Colors.Accent else if (enabled) Colors.Text2 else Colors.Text3
    val shape = RoundedCornerShape(un.dp(8f))
    var m = modifier.fillMaxWidth().height(un.dp(27f)).clip(shape)
    if (selected) m = m.background(Colors.AccentBg).border(BorderStroke(un.dp(1.5f), Colors.Accent), shape)
    if (requester != null) m = m.focusRequester(requester)
    m = m.onFocusChanged { focused = it.isFocused; if (it.isFocused) onFocus() }
    if (enabled) {
        m = m.onKeyEvent {
            if (it.key in selectKeys) {
                if (it.type == KeyEventType.KeyUp) onClick()
                true
            } else false
        }.pointerInput(onClick) { detectTapGestures { onClick() } }.focusable()
    }
    Row(m.padding(horizontal = un.dp(10f)), verticalAlignment = Alignment.CenterVertically) {
        Glyph(icon, un.dp(15f), ic)
        Spacer(Modifier.width(un.dp(10f)))
        Txt(label, 14.5f, fg, modifier = Modifier.weight(1f), lines = 1)
        if (value != null) {
            Spacer(Modifier.width(un.dp(8f)))
            Txt(value, 12.5f, if (selected) Colors.AccentText else Colors.Text2, lines = 1)
        }
        if (chevron) {
            Spacer(Modifier.width(un.dp(8f)))
            Glyph(Icon.Chevron, un.dp(13f), if (selected) Colors.Accent else Colors.Text3)
        }
    }
}

@Composable
fun Dialog(title: String, text: String, action: String, onAction: () -> Unit, onCancel: () -> Unit, other: String? = null, onOther: () -> Unit = {}) {
    val un = LocalUnits.current
    val first = remember { FocusRequester() }
    androidx.compose.runtime.LaunchedEffect(Unit) { first.requestFocus() }
    Box(
        Modifier.fillMaxSize().background(Colors.Scrim).pointerInput(Unit) { detectTapGestures { } },
        contentAlignment = Alignment.Center,
    ) {
        Column(
            Modifier.width(un.dp(240f)).clip(RoundedCornerShape(un.dp(11f))).background(Colors.Card)
                .border(BorderStroke(un.dp(1f), Colors.Line), RoundedCornerShape(un.dp(11f))).padding(un.dp(11f)),
        ) {
            Txt(title, 14.5f, Colors.Text, FontWeight.Medium)
            Txt(text, 12.5f, Colors.Text2, modifier = Modifier.padding(top = un.dp(5f), bottom = un.dp(8f)))
            Entry(Icon.Back, "Cancel", requester = first, onClick = onCancel)
            Entry(Icon.Check, action, onClick = onAction)
            if (other != null) Entry(Icon.Trash, other, tone = Tone.Danger, onClick = onOther)
        }
    }
}
