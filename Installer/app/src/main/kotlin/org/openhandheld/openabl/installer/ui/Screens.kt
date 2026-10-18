// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026-present Open Handheld Collective
//   https://github.com/OpenHandheldCollective

package org.openhandheld.openabl.installer.ui

import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.focusable
import androidx.compose.foundation.gestures.animateScrollBy
import androidx.compose.animation.core.animateFloat
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusProperties
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.onKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import org.openhandheld.openabl.installer.Installer
import org.openhandheld.openabl.installer.Screen
import org.openhandheld.openabl.installer.Updates
import org.openhandheld.openabl.installer.device.Chip
import org.openhandheld.openabl.installer.root.Route
import kotlinx.coroutines.launch

private sealed interface Line {
    class Head(val text: String) : Line
    class Text(val text: String, val danger: Boolean = false) : Line
    class Item(
        val icon: Icon,
        val label: String,
        val value: String? = null,
        val chevron: Boolean = false,
        val enabled: Boolean = true,
        val primary: Boolean = false,
        val onClick: () -> Unit,
    ) : Line
}

private val SUPPORTED = Chip.entries.joinToString(", ") { it.id }

private fun facts(i: Installer): List<Fact> {
    val d = i.device
    val insp = i.inspection
    val slot = { v: String? -> v ?: if (i.reading) "Reading" else if (i.route?.runs == false) "Not read" else "Unknown" }
    return listOf(
        Fact(Icon.Chip, "Chip", d.chip?.short ?: d.socModel.ifEmpty { "Unknown" }),
        Fact(Icon.Gamepad, "Model", d.model),
        Fact(Icon.Key, "Root", i.route?.label ?: "Looking", i.route == Route.None),
        Fact(Icon.Database, "Slot A", slot(insp?.slotA), insp == null),
        Fact(Icon.Database, "Slot B", slot(insp?.slotB), insp == null),
        Fact(Icon.Layers, "Image", i.ready?.let { "OpenABL ${it.version}" } ?: "None", i.ready == null),
    )
}

private fun homeLines(i: Installer, context: Context, finish: () -> Unit): List<Line> {
    val out = ArrayList<Line>()
    if (i.chip == null) {
        out += Line.Head("Not supported")
        out += Line.Text("OpenABL has no image for this chip. It supports $SUPPORTED.")
        out += Line.Head("About")
        out += Line.Item(Icon.File, "Credits", chevron = true) { i.screen = Screen.Licenses }
        out += Line.Item(Icon.Exit, "Close") { finish() }
        return out
    }
    if (i.route == null) return out
    val ready = i.ready
    val both = listOf(i.inspection?.slotA, i.inspection?.slotB)
    val label = when {
        ready != null && both.all { it == "OpenABL ${ready.version}" } -> "Reinstall OpenABL"
        both.any { it?.startsWith("OpenABL") == true } -> "Update OpenABL"
        else -> "Install OpenABL"
    }
    out += Line.Head("Install")
    if (!i.auto || i.inspectNote != null) {
        out += Line.Item(Icon.Search, "Read the slots", enabled = !i.reading && i.route != null) { i.inspect() }
    }
    out += Line.Item(Icon.Download, label, ready?.version, enabled = ready != null && !i.reading && i.route != null, primary = true) { i.askInstall() }
    when (val u = i.updates) {
        is Updates.Found -> out += Line.Item(Icon.Refresh, "Get OpenABL ${u.release.version}", "Download") { i.download(u.release) }
        Updates.Downloading -> out += Line.Item(Icon.Refresh, "Downloading", enabled = false) {}
        Updates.Checking, Updates.Idle -> out += Line.Item(Icon.Refresh, "Checking for updates", enabled = false) {}
        else -> out += Line.Item(
            Icon.Refresh, "Check for updates",
            when (u) { Updates.Current -> "Up to date"; Updates.Offline -> "Offline"; else -> "Failed" },
        ) { i.checkUpdates() }
    }
    out += Line.Head("Backups")
    out += Line.Item(Icon.History, "Restore a backup", i.inspection?.backups?.size?.toString(), chevron = true, enabled = i.route != null) { i.screen = Screen.Restore }
    out += Line.Head("Device")
    out += Line.Item(Icon.Power, "Reboot to bootloader", enabled = i.route != null && !i.reading) { i.askReboot() }
    out += Line.Head("About")
    out += Line.Item(Icon.File, "Credits", chevron = true) { i.screen = Screen.Licenses }
    i.inspectNote?.let { out += Line.Text(it, danger = true) }
    (i.updates as? Updates.Failed)?.let { out += Line.Text(it.why, danger = true) }
    if (!i.device.listed) out += Line.Text("OpenABL doesn't list this model. Check that it has abl_a and abl_b.")
    if (i.route == Route.None) out += Line.Text("No root found. The app saves a script in Download to run as root.")
    return out
}

private fun restoreLines(i: Installer): List<Line> {
    val out = ArrayList<Line>()
    out += Line.Head("Backups")
    val list = i.inspection?.backups
    when {
        list == null -> {
            out += Line.Text("The slots haven't been read.")
            out += Line.Item(Icon.Search, "Read the slots", enabled = !i.reading) { i.inspect() }
        }
        list.isEmpty() -> out += Line.Text("No backups in Download/OpenABL.")
        else -> list.forEach { n -> out += Line.Item(Icon.History, Installer.pretty(n)) { i.askRestore(n) } }
    }
    out += Line.Item(Icon.Back, "Back") { i.screen = Screen.Home }
    return out
}

@Composable
private fun Lines(lines: List<Line>, resetKey: Any, overlay: Boolean, modifier: Modifier) {
    val items = lines.filterIsInstance<Line.Item>()
    val requesters = remember(items.size, resetKey) { List(items.size) { FocusRequester() } }
    var current by remember(resetKey) { mutableStateOf<String?>(null) }
    val target = items.indexOfFirst { it.label == current && it.enabled }.takeIf { it >= 0 }
        ?: items.indexOfFirst { it.primary && it.enabled }.takeIf { it >= 0 }
        ?: items.indexOfFirst { it.enabled }.coerceAtLeast(0)
    LaunchedEffect(overlay, resetKey, items.size) {
        if (!overlay && requesters.isNotEmpty()) runCatching { requesters[target].requestFocus() }
    }
    val un = LocalUnits.current
    val scroll = rememberScrollState()
    Box(modifier) {
        Column(Modifier.fillMaxSize().verticalScroll(scroll).focusProperties { canFocus = !overlay }) {
            var n = 0
            lines.forEach { l ->
                when (l) {
                    is Line.Head -> Section(l.text, first = l === lines.first())
                    is Line.Text -> Note(l.text, if (l.danger) Colors.Danger else Colors.Text3)
                    is Line.Item -> {
                        val idx = n++
                        Entry(l.icon, l.label, value = l.value, chevron = l.chevron, enabled = l.enabled, requester = requesters[idx], onFocus = { current = l.label }, onClick = l.onClick)
                    }
                }
            }
        }
        if (scroll.canScrollForward) {
            Glyph(Icon.Down, un.dp(13f), Colors.Text3, Modifier.align(Alignment.BottomCenter))
        }
    }
}

@Composable
fun App(i: Installer, finish: () -> Unit) {
    val context = LocalContext.current
    Themed { landscape ->
        val un = LocalUnits.current
        val overlay = i.confirm != null
        val hints = buildList {
            add(Hint("Up and Down", icon = Icon.UpDown))
            add(Hint("Select", key = "A"))
            if (i.screen != Screen.Home) add(Hint("Back", key = "B"))
        }
        Box(Modifier.fillMaxSize().background(Colors.Background)) {
            Column(Modifier.fillMaxSize().padding(horizontal = un.dp(if (landscape) 13f else 12f))) {
                val checking = i.updates == Updates.Checking || i.updates == Updates.Idle
                Header(i.bundled, i.app?.version, checking) {
                    val a = i.app
                    if (a != null) runCatching { context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(a.page))) }
                    else i.checkUpdates()
                }
                if (i.screen == Screen.Licenses) {
                    Licenses(context, Modifier.weight(1f).fillMaxWidth())
                } else if (landscape) {
                    Row(Modifier.weight(1f).fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(un.dp(13f))) {
                        Left(i, overlay, finish, context, Modifier.weight(3f).fillMaxSize())
                        SystemPanel(facts(i), Modifier.weight(2f).padding(bottom = un.dp(9f)))
                    }
                } else {
                    SystemPanel(facts(i), Modifier.fillMaxWidth().padding(bottom = un.dp(6f)))
                    Left(i, overlay, finish, context, Modifier.weight(1f).fillMaxWidth())
                }
                Footer(hints, Modifier.height(un.dp(36f)))
            }
            i.confirm?.let { c ->
                Dialog(c.title, c.text, c.action, { c.run() }, { i.confirm = null }, c.other, { c.runOther() })
            }
        }
    }
}

@Composable
private fun Left(i: Installer, overlay: Boolean, finish: () -> Unit, context: Context, modifier: Modifier) {
    when (i.screen) {
        Screen.Job -> JobView(i, overlay, modifier)
        Screen.Restore -> Lines(restoreLines(i), "restore", overlay, modifier)
        else -> Lines(homeLines(i, context, finish), "home", overlay, modifier)
    }
}

@Composable
private fun JobView(i: Installer, overlay: Boolean, modifier: Modifier) {
    val r = i.running ?: return
    val un = LocalUnits.current
    val done = r.result != null
    val failed = r.result?.failed == true
    val focus = remember { FocusRequester() }
    LaunchedEffect(done, r.script) { if (done || r.script != null) runCatching { focus.requestFocus() } }
    Column(modifier.verticalScroll(rememberScrollState()).focusProperties { canFocus = !overlay }) {
        Section(r.title)
        Txt(r.message, 14.5f, if (failed) Colors.Danger else Colors.Text, modifier = Modifier.padding(horizontal = un.dp(10f), vertical = un.dp(4f)))
        if (r.script != null) {
            Note("Saved as Download/${r.script}. Run it as root with your handheld's own tool. On Retroid that is Settings, Handheld Settings, Advanced, Run script as root.")
        } else if (!done) {
            Progress()
        }
        if (r.keys) {
            Section("At power-on")
            Keys.forEach { (k, v) ->
                Row(Modifier.fillMaxWidth().padding(horizontal = un.dp(10f), vertical = un.dp(2f))) {
                    Txt(k, 12.5f, Colors.Text, modifier = Modifier.width(un.dp(70f)))
                    Txt(v, 12.5f, Colors.Text2, modifier = Modifier.weight(1f))
                }
            }
            Note("In the menu, Volume Up and Down move the selection and Power selects.")
        }
        if (done || r.script != null) {
            Spacer(Modifier.height(un.dp(8f)))
            Entry(if (done) Icon.Check else Icon.Back, if (done) "Done" else "Cancel", requester = focus) {
                if (done) i.finish() else i.back()
            }
        }
    }
}

private val Keys = listOf(
    "Nothing" to "Boots the selected boot mode.",
    "Volume Down" to "Opens the menu.",
    "Volume Up" to "Boots Android when the boot mode is Linux.",
)

@Composable
private fun Progress() {
    val un = LocalUnits.current
    val t = androidx.compose.animation.core.rememberInfiniteTransition(label = "sweep")
    val x by t.animateFloat(
        0f, 1f,
        androidx.compose.animation.core.infiniteRepeatable(androidx.compose.animation.core.tween(1100, easing = androidx.compose.animation.core.LinearEasing)),
        label = "x",
    )
    Box(Modifier.padding(horizontal = un.dp(10f), vertical = un.dp(6f)).fillMaxWidth().height(un.dp(2f)).background(Colors.Line)) {
        androidx.compose.foundation.Canvas(Modifier.fillMaxSize()) {
            val w = size.width * 0.3f
            val left = (size.width + w) * x - w
            drawRect(
                Colors.Accent,
                androidx.compose.ui.geometry.Offset(left.coerceAtLeast(0f), 0f),
                androidx.compose.ui.geometry.Size((left + w).coerceAtMost(size.width) - left.coerceAtLeast(0f), size.height),
            )
        }
    }
}

@Composable
private fun Licenses(context: Context, modifier: Modifier) {
    val un = LocalUnits.current
    val scroll = rememberScrollState()
    val scope = rememberCoroutineScope()
    val focus = remember { FocusRequester() }
    LaunchedEffect(Unit) { runCatching { focus.requestFocus() } }
    val ofl = remember { runCatching { context.assets.open("licenses/Montserrat-OFL.txt").bufferedReader().readText() }.getOrDefault("") }
    val mit = remember { runCatching { context.assets.open("licenses/MIT.txt").bufferedReader().readText() }.getOrDefault("") }
    val apache = remember { runCatching { context.assets.open("licenses/Apache-2.0.txt").bufferedReader().readText() }.getOrDefault("") }
    val step = with(androidx.compose.ui.platform.LocalDensity.current) { un.dp(60f).toPx() }
    Column(
        modifier.focusRequester(focus).focusable().onKeyEvent {
            if (it.type != KeyEventType.KeyDown) return@onKeyEvent false
            when (it.key) {
                Key.DirectionDown -> { scope.launch { scroll.animateScrollBy(step) }; true }
                Key.DirectionUp -> { scope.launch { scroll.animateScrollBy(-step) }; true }
                else -> false
            }
        }.verticalScroll(scroll).padding(horizontal = un.dp(10f)),
    ) {
        Spacer(Modifier.height(un.dp(8f)))
        Txt("OpenABL", 14.5f, Colors.Text, FontWeight.Medium)
        Para("Developed by the Open Handheld Collective. Free software under the GPL-3.0-or-later, with no warranty.")
        Link("github.com/OpenHandheldCollective/OpenABL")
        Spacer(Modifier.height(un.dp(10f)))
        Txt("Android Installer", 14.5f, Colors.Text, FontWeight.Medium)
        Para("Developed by Aimal (hashtagbasit). Installs, updates, backs up and restores OpenABL on your handheld from Android, without a PC.")
        Link("github.com/hashtagbasit")
        Spacer(Modifier.height(un.dp(14f)))
        Txt("Also uses", 12.5f, Colors.Text3, FontWeight.Medium)
        Minor("Montserrat font", "Copyright 2011 The Montserrat Project Authors. SIL Open Font License 1.1.", ofl)
        Minor("Tabler Icons", "The icons are drawn after Tabler Icons by Paweł Kuna. MIT License.", mit)
        Minor("AndroidX, Jetpack Compose and Kotlin", "Apache License 2.0.", apache)
        Spacer(Modifier.height(un.dp(12f)))
    }
}

@Composable
private fun Link(text: String) {
    val un = LocalUnits.current
    val uri = androidx.compose.ui.platform.LocalUriHandler.current
    Txt(
        text, 12.5f, Colors.Brand,
        modifier = Modifier.padding(top = un.dp(2f)).clickable { runCatching { uri.openUri("https://$text") } },
    )
}

@Composable
private fun Minor(name: String, line: String, license: String) {
    val un = LocalUnits.current
    Txt(name, 11f, Colors.Text2, FontWeight.Medium, Modifier.padding(top = un.dp(8f)))
    Txt(line, 11f, Colors.Text3, modifier = Modifier.padding(top = un.dp(2f)))
    if (license.isNotEmpty()) Txt(license, 10f, Colors.Text3, modifier = Modifier.padding(top = un.dp(3f)))
}

@Composable
private fun Para(text: String, color: androidx.compose.ui.graphics.Color = Colors.Text2) {
    val un = LocalUnits.current
    Txt(text, 12.5f, color, modifier = Modifier.padding(top = un.dp(3f)))
}
