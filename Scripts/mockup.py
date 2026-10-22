#!/usr/bin/env python3
"""PortABL's menu as portarelauncher draws a screen: the 53x20 grid, the
8x16 font at scale 3, the amber palette. Grid, glyphs and rasteriser are
the launcher's own tools/mockup.py and tools/mockup_png.py, so a screen
here and a launcher screen render the same.

    python3 Scripts/mockup.py [portarelauncher checkout]

Writes Docs/mockup/*.png and prints each screen as text.
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
LAUNCHER = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "portarelauncher")
sys.path.insert(0, os.path.join(LAUNCHER, "tools"))
sys.dont_write_bytecode = True  # nothing left behind in the launcher's tree
import mockup as L
import mockup_png as P

COLS, ROWS = L.COLS, L.ROWS
RULE, THIN, row, item = L.RULE, L.THIN, L.row, L.item
VERSION = open(os.path.join(HERE, "..", "VERSION")).read().strip()
ARROW = "→"

# No clock: the ABL has the RTC's UTC, not the launcher's local time.
def header(*crumbs):
    return L.header("  ›  ".join(("PortABL",) + crumbs), "v%s " % VERSION)

# The Nova's keys: the volume rocker moves, power acts.
def footer(verb, right="VOL MOVE "):
    return row(" POWER " + verb, right)

def wrap(text, indent=4):
    out, line = [], ""
    for word in text.split():
        if line and L.width(line) + 1 + L.width(word) > COLS - indent - 2:
            out.append(" " * indent + line)
            line = word
        else:
            line = (line + " " + word) if line else word
    return out + [" " * indent + line]

def fill(lines, upto):
    return lines + [""] * (upto - len(lines))

def label(text):
    return "  " + text.upper()

# A row is a section label, a rule, or (name, value, footer verb). Rows
# that do not apply are left out rather than greyed: Install only with
# PortareOS on a card and none inside, the uninstalls only with something
# to uninstall, Android and Boot order only while Android is there.
def menu_screen(rows, selected, note=None):
    lines = [header(), RULE, ""]
    verb = "SELECT"
    for entry in rows:
        if isinstance(entry, str):
            lines.append(entry)
            continue
        name, value, action = entry
        lines.append(item(name == selected, name, value))
        if name == selected:
            verb = action
    lines = fill(lines, 16 if note else ROWS - 2)
    if note:
        lines += [THIN, "    " + note]
    return lines + [THIN, footer(verb)]

SYSTEM = [THIN, label("System"),
          ("Emergency download mode", "", "SELECT"),
          ("Restart", "", "SELECT"),
          ("Shut down", "", "SELECT")]

installed = [label("Start"),
             ("PortareOS", "Internal", "LAUNCH"),
             ("Android", "", "LAUNCH"),
             ("Boot order", "PortareOS, Android", "CHANGE"),
             THIN, label("Storage"),
             ("Uninstall PortareOS", "", "SELECT"),
             ("Uninstall Android", "", "SELECT")] + SYSTEM
menu = menu_screen(installed, "PortareOS",
                   "Starts at power-on. Hold VOL+ for Android.")

# Retroid's Android inside, a PortareOS card in the slot.
first_time = [label("Start"),
              ("PortareOS", "SD card", "LAUNCH"),
              ("Android", "", "LAUNCH"),
              ("Boot order", "PortareOS, Android", "CHANGE"),
              THIN, label("Storage"),
              ("Install PortareOS", "", "SELECT")] + SYSTEM
menu_first_time = menu_screen(first_time, "Install PortareOS",
                              "Copies PortareOS from the card to the Nova.")

# After Uninstall Android: nothing else to start, nothing to uninstall.
android_gone = [label("Start"), ("PortareOS", "Internal", "LAUNCH")] + SYSTEM
menu_no_android = menu_screen(android_gone, "PortareOS")

def confirm(crumb, question, body, extra, yes, sel="Cancel"):
    lines = [header(crumb), RULE, "", "  " + question, ""]
    lines += wrap(body)
    lines += extra
    lines += ["", item(sel == "Cancel", "Cancel"), item(sel == yes, yes)]
    return fill(lines, ROWS - 2) + [THIN, footer("SELECT")]

# A fresh Nova: userdata is everything after super, 108 GB.
confirm_install = [header("Install PortareOS"), RULE, "",
                   "  Install PortareOS?", ""]
confirm_install += wrap("Copies PortareOS from the SD card to internal "
                        "storage. Android's data is erased to make room.")
confirm_install += ["", item(True, "Android keeps", "16 GB"),
                    row("    PortareOS gets", "92 GB  "),
                    "", item(False, "Cancel"), item(False, "Install")]
confirm_install = fill(confirm_install, ROWS - 2) + [THIN, footer("CHANGE")]

installing = [header("Install PortareOS"), RULE] + [""] * 4
installing += ["           Installing PortareOS", "",
               "           Copying SYSTEM, 1.4 of 2.1 GB", "",
               "           Don't turn off the Nova."]
installing = fill(installing, ROWS)

# This Nova: super and userdata sit in front of PortareOS.
confirm_uninstall_android = confirm(
    "Uninstall Android", "Uninstall Android?",
    "Erases Android and all of its data, and gives the space to PortareOS:",
    ["", row("    super", "12.9 GB    "), row("    userdata", "1.1 GB    "),
     "", row("    PortareOS storage", "105 %s 119 GB    " % ARROW)],
    "Uninstall")

confirm_uninstall_portareos = confirm(
    "Uninstall PortareOS", "Uninstall PortareOS?",
    "Erases PortareOS with its games and saves. Android gets the space "
    "back and starts fresh:",
    ["", row("    PORTAREOS", "2.1 GB    "), row("    PORTAREOS_STOR", "105 GB    ")],
    "Uninstall")

confirm_edl = confirm("Emergency download mode", "Enter emergency download mode?",
                      "The screen stays off in this mode. You'll need a computer "
                      "with Qualcomm flashing tools to leave it.", [], "Enter EDL")

# Centred like the launcher's Launching screen.
error = [header(), RULE] + [""] * 4
error += ["           Couldn't start PortareOS", ""]
error += wrap("PortareOS isn't on internal storage or an SD card.", 11)
error += [""] + wrap("Insert a PortareOS card and choose Install PortareOS.", 11)
error = fill(error, ROWS - 2) + [THIN, footer("MENU", "")]
ERROR_TITLE = (6, 11, 34)

starting = [header(), RULE] + [""] * 4
starting += ["           PortareOS", "", "           Starting..."]
starting = fill(starting, ROWS)

SCREENS = [("menu", "MAIN MENU", menu, None),
           ("menu-first-time", "MAIN MENU, FIRST TIME", menu_first_time, None),
           ("menu-no-android", "MAIN MENU, ANDROID UNINSTALLED", menu_no_android, None),
           ("confirm-install", "INSTALL PORTAREOS", confirm_install, None),
           ("installing", "INSTALLING", installing, None),
           ("confirm-uninstall-android", "UNINSTALL ANDROID",
            confirm_uninstall_android, None),
           ("confirm-uninstall-portareos", "UNINSTALL PORTAREOS",
            confirm_uninstall_portareos, None),
           ("confirm-edl", "CONFIRM EDL", confirm_edl, None),
           ("error-no-portareos", "NO PORTAREOS", error, ([ERROR_TITLE], [])),
           ("starting", "STARTING", starting, None)]

if __name__ == "__main__":
    out = os.path.join(HERE, "..", "Docs", "mockup")
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        if f.endswith(".png"):
            os.remove(os.path.join(out, f))
    glyphs = P.font()
    for name, title, lines, spans in SCREENS:
        assert len(lines) == ROWS, (name, len(lines))
        print(L.screen(title, lines))
        print()
        P.render(lines, os.path.join(out, name + ".png"), glyphs, spans)
