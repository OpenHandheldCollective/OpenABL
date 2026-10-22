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

# The whole main menu as the Nova shows it: A/B slots, an internal install
# (so Boot source and Uninstall CFW), the menu built into the ABL (so no
# Menu timeout), and two places Start can boot from.
MENU = [label("Boot"),
        ("Start Linux", "", "START"),
        ("Boot entries", "2", "OPEN"),
        ("Restart bootloader", "", "SELECT"),
        ("Recovery mode", "", "SELECT"),
        ("Emergency download mode", "", "SELECT"),
        ("Boot other slot", "B", "SELECT"),
        THIN,
        label("Settings"),
        ("Language", "English", "CHANGE"),
        ("Boot mode", "Linux", "CHANGE"),
        ("ABL mode", "Silent", "CHANGE"),
        ("Volume up at boot", "Android", "CHANGE"),
        ("Boot source", "Auto", "CHANGE"),
        ("Off-mode charging", "Off", "CHANGE"),
        ("Display rotation", "Auto (90°)", "CHANGE"),
        ("Custom boot logo", "Off", "CHANGE"),
        ("Device model", "Retroid Pocket Nova", "OPEN"),
        THIN,
        label("System"),
        ("Log", "", "OPEN"),
        ("Device info", "", "OPEN"),
        ("Uninstall CFW", "", "SELECT"),
        ("Power off", "", "SELECT")]

def menu_screen(selected, top, note=None):
    """The list from row 3, scrolled to start at MENU[top]; a note takes
    the block above the footer, as the launcher's Tools screen does."""
    last = 15 if note else 17
    lines = [header(), RULE, ""]
    verb = "SELECT"
    for entry in MENU[top:top + last - 2]:
        if isinstance(entry, str):
            lines.append(entry)
        else:
            name, value, action = entry
            lines.append(item(name == selected, name, value))
            if name == selected:
                verb = action
    lines = fill(lines, last + 1)
    if note:
        lines += [THIN, "    " + note]
    return lines + [THIN, footer(verb)]

menu = menu_screen("Start Linux", 0,
                   "Looking for Linux on the first bootable drive.")
menu_settings = menu_screen("Boot source", 9)

ENTRIES = [("Boot image", "Internal storage"), ("Boot image", "SD card")]
entries = [header("Boot entries"), RULE, ""]
entries += [item(i == 0, n, v) for i, (n, v) in enumerate(ENTRIES)]
entries += ["", item(False, "Back")]
entries = fill(entries, 16)
entries += [THIN, "    KERNEL on PORTAREOS. Start tries it first.",
            THIN, footer("START")]

INFO = [("Chip", "QCS8550"), ("RAM", "8 GB"), ("Storage", "128 GB UFS"),
        ("Storage chip", "YMTC YMUS8B4TF1D1C1"), ("SD card", "Not inserted"),
        ("Display", "960 x 1280, 90°"), ("Model", "Retroid Pocket Nova"),
        ("Bootloader", "PortABL " + VERSION)]
device_info = [header("Device info"), RULE, ""]
device_info += [item(False, n, v) for n, v in INFO]
device_info = fill(device_info, ROWS - 2) + [THIN, footer("BACK", "")]

# What the ABL wrote while looking, as the Log screen lists it.
LOG = ["Boot mode Linux, boot source Auto",
       "Internal storage first: CFW after userdata",
       "sda18 PORTAREOS: no loader entries",
       "sda18 PORTAREOS: no extlinux.conf",
       "sda18 PORTAREOS: no EFI\\BOOT\\BOOTAA64.EFI",
       "sda18 PORTAREOS: boot image KERNEL, 46 MB",
       "SD card: not inserted",
       "2 boot entries, starting the first"]
log = [header("Log"), RULE, ""]
log += ["  " + l for l in LOG]
log = fill(log, ROWS - 2) + [THIN, footer("BACK", "VOL SCROLL ")]

def confirm(crumb, question, body, extra, yes):
    lines = [header(crumb), RULE, "", "  " + question, ""]
    lines += wrap(body)
    lines += extra
    lines += ["", item(True, "Cancel"), item(False, yes)]
    return fill(lines, ROWS - 2) + [THIN, footer("SELECT")]

confirm_edl = confirm("Emergency download mode", "Enter emergency download mode?",
                      "The screen stays off in this mode. You'll need a computer "
                      "with Qualcomm flashing tools to leave it.", [], "Enter EDL")

confirm_uninstall = confirm("Uninstall CFW", "Uninstall CFW?",
                            "Erases these partitions, and all of Android's data "
                            "with them:",
                            ["", row("    PORTAREOS", "2.1 GB    "),
                             row("    PORTAREOS_STOR", "105 GB    ")], "Uninstall")

# Centred like the launcher's Launching screen.
error = [header(), RULE] + [""] * 4
error += ["           Couldn't boot Linux", ""]
error += wrap("No bootable image was found on the selected boot source.", 11)
error += [""] + wrap("Check the drive, or change Boot source in the menu.", 11)
error = fill(error, ROWS - 2) + [THIN, footer("MENU", "")]
ERROR_TITLE = (6, 11, 29)

starting = [header(), RULE] + [""] * 4
starting += ["           Internal storage, boot image", "",
             "           Starting Linux..."]
starting = fill(starting, ROWS)

SCREENS = [("menu", "MAIN MENU", menu, None),
           ("menu-settings", "MAIN MENU, SCROLLED TO SETTINGS", menu_settings, None),
           ("boot-entries", "BOOT ENTRIES", entries, None),
           ("device-info", "DEVICE INFO", device_info, None),
           ("log", "LOG", log, None),
           ("confirm-edl", "CONFIRM EDL", confirm_edl, None),
           ("confirm-uninstall", "CONFIRM UNINSTALL", confirm_uninstall, None),
           ("error-no-image", "NO BOOTABLE IMAGE", error, ([ERROR_TITLE], [])),
           ("starting", "STARTING", starting, None)]

if __name__ == "__main__":
    out = os.path.join(HERE, "..", "Docs", "mockup")
    os.makedirs(out, exist_ok=True)
    glyphs = P.font()
    for name, title, lines, spans in SCREENS:
        assert len(lines) == ROWS, (name, len(lines))
        print(L.screen(title, lines))
        print()
        P.render(lines, os.path.join(out, name + ".png"), glyphs, spans)
