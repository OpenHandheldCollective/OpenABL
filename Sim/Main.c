/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Main - renders the menu's screens to PNG on the host.

    openabl-menu-sim [--out DIR] [--filter TEXT] [--check]

  For every panel and rotation it writes the main menu as the user sees it
  (*-view.png) and as the panel receives it (*-scanout.png, rotated). A few
  panel and rotation pairs also get every other screen. --check instead
  runs sessions of key presses and compares each frame with a full redraw
  and with a freshly built view.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "Core/Settings.h"
#include "Base/Text.h"
#include "Base/Memory.h"
#include "Ui/Lang.h"
#include "Ui/Ui.h"
#include "View/View.h"

STATIC CONST INT32 mPanels[][2] = {{1080, 1920}, {960, 1280},  {720, 1280},
                                   {1440, 2560}, {1920, 1080}, {1240, 1080},
                                   {1280, 800}};
STATIC CONST INT32 mRotations[] = {0, 90, 180, 270};
// These get every screen: portrait and landscape 16:9, landscape 4:3 and
// 1280x800.
STATIC CONST INT32 mFullSet[][3] = {{1080, 1920, 0},
                                    {1080, 1920, 270},
                                    {960, 1280, 90},
                                    {1280, 800, 0}};

STATIC UINT32  mClockMs;
STATIC UINT32 *mLogical, *mPhysical;
STATIC INT32   mPw;

// Simulated time, in milliseconds.
STATIC UINT32
Tick (VOID)
{
  return mClockMs;
}

STATIC VOID
BlitToPanel (
  IN CONST UINT32 *Px,
  IN UINT32        Stride,
  IN UINT32        X,
  IN UINT32        Y,
  IN UINT32        W,
  IN UINT32        H
)
{
  for (UINT32 Row = 0; Row < H; Row++) {
    CopyMem (
      &mPhysical[(UINTN)(Y + Row) * mPw + X], &Px[(UINTN)Row * Stride],
      (UINTN)W * 4
    );
  }
}

STATIC VOID
Chunk (
  IN OUT FILE    *File,
  IN CONST CHAR8 *Type,
  IN CONST UINT8 *Data OPTIONAL,
  IN UINT32       Len
)
{
  UINT8 Length[4] = {Len >> 24, Len >> 16, Len >> 8, Len};
  fwrite (Length, 1, 4, File);
  fwrite (Type, 1, 4, File);
  fwrite (Data, 1, Len, File);
  uLong Crc         = crc32 (crc32 (0, (CONST Bytef *)Type, 4), Data, Len);
  UINT8 CrcBytes[4] = {Crc >> 24, Crc >> 16, Crc >> 8, Crc};
  fwrite (CrcBytes, 1, 4, File);
}

// Writes 0x00RRGGBB pixels as an RGB PNG. Exits if Path can't be opened.
STATIC VOID
WritePng (
  IN CONST CHAR8  *Path,
  IN INT32         W,
  IN INT32         H,
  IN CONST UINT32 *Px
)
{
  UINTN  RowSize       = 1 + (UINTN)W * 3;
  UINTN  RawLen        = RowSize * H;
  UINT8 *Raw           = malloc (RawLen);
  uLongf CompressedLen = compressBound (RawLen);
  UINT8 *Compressed    = malloc (CompressedLen);

  for (INT32 Y = 0; Y < H; Y++) {
    UINT8 *Out = Raw + RowSize * Y;
    *Out++     = 0;
    for (INT32 X = 0; X < W; X++) {
      UINT32 Pixel = Px[(UINTN)Y * W + X];
      *Out++       = Pixel >> 16;
      *Out++       = Pixel >> 8;
      *Out++       = Pixel;
    }
  }
  compress2 (Compressed, &CompressedLen, Raw, RawLen, 6);
  FILE *File = fopen (Path, "wb");
  if (!File) {
    perror (Path);
    exit (1);
  }
  STATIC CONST UINT8 Signature[8] = {0x89, 'P',  'N',  'G',
                                     '\r', '\n', 0x1a, '\n'};
  UINT8 Ihdr[13] = {W >> 24, W >> 16, W >> 8, W, H >> 24, H >> 16, H >> 8,
                    H,       8,       2,      0, 0,       0};
  fwrite (Signature, 1, 8, File);
  Chunk (File, "IHDR", Ihdr, 13);
  Chunk (File, "IDAT", Compressed, (UINT32)CompressedLen);
  Chunk (File, "IEND", NULL, 0);
  fclose (File);
  free (Raw);
  free (Compressed);
}

// Sample device info, models and boot entries

STATIC VOID
SampleInfo (OUT DEVICE_INFO *Info)
{
  *Info = (DEVICE_INFO){0};
  AsciiStrCopy (Info->Soc, sizeof (Info->Soc), "Snapdragon 865 (SM8250)");
  AsciiStrCopy (Info->Ram, sizeof (Info->Ram), "8 GB");
  AsciiStrCopy (Info->Storage, sizeof (Info->Storage), "128 GB UFS");
  AsciiStrCopy (Info->SdCard, sizeof (Info->SdCard), "512 GB");
  AsciiStrCopy (Info->Firmware, sizeof (Info->Firmware), "simulator");
}

STATIC CONST MODEL mModels[] = {
  {"Retroid Pocket 5", 270, "SD card \\dtbs"},
  {"Retroid Pocket Mini", 90, "SD card \\dtbs"},
  {"Retroid Pocket Mini V2", 90, "SD card \\dtbs"},
  {"Retroid Pocket Flip 2", 270, "SD card \\dtbs"},
  {"AYN Thor Lite", 90, "SD card \\dtbs"},
  {"MANGMI Pocket Max", 270, "SD card \\dtbs"},
  {"Mangmi Air Y Pro", ROTATION_NONE, "SD card \\dtbs"},
};

STATIC UINTN
SampleModels (OUT CONST MODEL **Models)
{
  *Models = mModels;
  return sizeof (mModels) / sizeof (mModels[0]);
}

// The boot list on a Nova with PortareOS inside and a PortareOS card.
STATIC CONST ENTRY_VIEW mEntries[] = {
  {.Title = "PortareOS", .Drive = "PORTAREOS", .Kind = ENTRY_KIND_BOOT_IMAGE},
  {.Title     = "PortareOS",
   .Drive     = "PORTAREOS",
   .Kind      = ENTRY_KIND_BOOT_IMAGE,
   .Removable = TRUE},
  {.Title = "Android", .Kind = ENTRY_KIND_ANDROID},
};

// A stock Nova (Android alone, userdata from 14520 MiB of 117776) with a
// PortareOS card in.
#define MIB (1ull << 20)
STATIC CONST INSTALL_INFO mInstall = {
  .Card           = TRUE,
  .Known          = TRUE,
  .Android        = TRUE,
  .Dualboot       = TRUE,
  .Replace        = TRUE,
  .Shared         = 103255 * MIB,
  .AndroidMin     = 4ull << 30,
  .AndroidMax     = 91ull << 30,
  .BootBytes      = 2048 * MIB,
  .UpdateStorage  = 100183 * MIB,
  .ReplaceStorage = 113496 * MIB,
};

// Screens

STATIC SETTINGS_CAPS
SampleCaps (VOID)
{
  return (SETTINGS_CAPS){
    .Android = TRUE, .UsbHost = TRUE, .CfwOnInternal = TRUE};
}

STATIC VOID
MakeApp (
  OUT APP           *A,
  IN CONST SETTINGS *Settings
)
{
  DEVICE_INFO   Info;
  SETTINGS_CAPS Caps     = SampleCaps ();
  SETTINGS      Language = *Settings;
  SampleInfo (&Info);
  Language.Language = LANG_EN;
  AppInit (A, &Language, &Caps, &Info, SCHEME_HANDHELD, MENU_VERSION);
  AppSetEntries (A, mEntries, sizeof (mEntries) / sizeof (mEntries[0]));
  AppSetInstall (A, &mInstall);
}

// Takes A, started at Now, to one of the screens.
typedef VOID (*SESSION_SETUP) (
  IN OUT APP *A,
  IN UINT64   Now
);

STATIC VOID
SMenu (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  (VOID)A, (VOID)Now;
}

STATIC VOID
SCountdown (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  SETTINGS Settings     = A->Settings;
  Settings.MenuTimeoutS = 5;
  MakeApp (A, &Settings);
  AppStart (A, Now, FALSE);
  AppTick (A, Now + 1200);
}

// After Replace Android: only PortareOS starts.
STATIC VOID
SNoAndroid (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  SETTINGS_CAPS Caps = SampleCaps ();

  (VOID)Now;
  Caps.Android = FALSE;
  AppSetCaps (A, &Caps);
}

// What Boot order does, and the CHANGE it takes.
STATIC VOID
SBootOrder (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  (VOID)Now;
  AppInput (A, INPUT_DOWN);
}

// A debug build's menu doesn't fit, so it scrolls to the last item.
STATIC VOID
SScrolled (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  SETTINGS_CAPS Caps = SampleCaps ();

  (VOID)Now;
  Caps.Embedded = TRUE;
  AppSetCaps (A, &Caps);
  A->Debug = TRUE;
  AppInput (A, INPUT_UP);
}

STATIC VOID
SModels (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  CONST MODEL *Models;
  UINTN        Count = SampleModels (&Models);
  (VOID)Now;
  AppOnModels (A, Models, Count);
  AppInput (A, INPUT_DOWN);
}

// Emergency download mode, with Enter EDL picked.
STATIC VOID
SConfirmEdl (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  (VOID)Now;
  for (INT32 Index = 0; Index < 3; Index++) {
    AppInput (A, INPUT_DOWN);
  }
  AppInput (A, INPUT_SELECT);
  AppInput (A, INPUT_DOWN);
}

// Boot opens the boot list.
STATIC VOID
SBootList (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  (VOID)Now;
  AppInput (A, INPUT_SELECT);
}

// The boot list on PortareOS from the SD card.
STATIC VOID
SBootListSd (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  SBootList (A, Now);
  AppInput (A, INPUT_DOWN);
}

STATIC VOID
SBooting (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  SBootList (A, Now);
  AppInput (A, INPUT_SELECT);
}

STATIC VOID
SBootingSd (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  SBootListSd (A, Now);
  AppInput (A, INPUT_SELECT);
}

STATIC VOID
SBootingAndroid (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  SBootList (A, Now);
  AppInput (A, INPUT_DOWN);
  AppInput (A, INPUT_DOWN);
  AppInput (A, INPUT_SELECT);
}

STATIC VOID
SErrorNoImage (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  (VOID)Now;
  AppOnBootResult (A, FALSE, BOOT_ERROR_NO_IMAGE, NULL);
}

STATIC VOID
SErrorModel (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  (VOID)Now;
  AppOnBootResult (A, FALSE, BOOT_ERROR_MODEL_NOT_SET, NULL);
}

STATIC VOID
SToast (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  (VOID)Now;
  AppNotify (A, "Settings saved");
}

// The SD card was taken out while its PortareOS was picked.
STATIC VOID
SErrorPickGone (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  (VOID)Now;
  AppOnPickFailed (A, TRUE, "PortareOS", TRUE);
}

// PortareOS on the SD card was there but didn't start.
STATIC VOID
SErrorPickFailed (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  (VOID)Now;
  AppOnPickFailed (A, FALSE, "PortareOS", TRUE);
}

// Install PortareOS's choices, Dualboot focused.
STATIC VOID
SInstall (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  (VOID)Now;
  AppInput (A, INPUT_DOWN);
  AppInput (A, INPUT_DOWN);
  AppInput (A, INPUT_SELECT);
}

STATIC VOID
SInstallReplace (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  SInstall (A, Now);
  AppInput (A, INPUT_DOWN);
}

// PortareOS next to Android already: Dualboot updates it.
STATIC VOID
SInstallUpdate (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  INSTALL_INFO Info = mInstall;

  Info.Update    = TRUE;
  Info.PortareOs = TRUE;
  AppSetInstall (A, &Info);
  SInstall (A, Now);
}

// Partitions BIOS doesn't know: neither choice runs.
STATIC VOID
SInstallUnknown (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  INSTALL_INFO Info = mInstall;

  Info.Known    = FALSE;
  Info.Dualboot = FALSE;
  Info.Replace  = FALSE;
  AppSetInstall (A, &Info);
  SInstall (A, Now);
}

STATIC VOID
SInstallSize (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  SInstall (A, Now);
  AppInput (A, INPUT_SELECT);
}

// Android keeping 16 GB, with Install picked.
STATIC VOID
SConfirmDualboot (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  SInstallSize (A, Now);
  AppInput (A, INPUT_SELECT);
  AppInput (A, INPUT_DOWN);
}

STATIC VOID
SConfirmUpdate (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  SInstallUpdate (A, Now);
  AppInput (A, INPUT_SELECT);
  AppInput (A, INPUT_DOWN);
}

// Replace Android over a Dualboot install.
STATIC VOID
SConfirmReplace (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  SInstallUpdate (A, Now);
  AppInput (A, INPUT_DOWN);
  AppInput (A, INPUT_SELECT);
  AppInput (A, INPUT_DOWN);
}

STATIC VOID
SInstalling (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  SConfirmDualboot (A, Now);
  AppInput (A, INPUT_SELECT);
  AppOnInstallProgress (A, INSTALL_STAGE_COPY, 437);
}

STATIC VOID
SInstalled (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  SInstalling (A, Now);
  AppOnInstalled (A, TRUE, NULL, TRUE);
}

STATIC VOID
SErrorNoCard (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  INSTALL_INFO Info = mInstall;

  Info.Card = FALSE;
  AppSetInstall (A, &Info);
  SInstall (A, Now);
}

// A write failed after the table changed.
STATIC VOID
SErrorInstall (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  CONST CHAR8 *Args[1] = {"error 7"};
  CHAR8        Why[192];

  SInstalling (A, Now);
  LangFill (Why, sizeof (Why), gStrings->InstallWriteFailed, Args, 1);
  AppOnInstalled (A, FALSE, Why, TRUE);
}

// Powered on by the charger, about half charged.
STATIC VOID
SCharging (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  (VOID)Now;
  AppStartCharging (A);
  AppOnBattery (A, TRUE, TRUE, 4010);
}

STATIC VOID
SChargingLow (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  (VOID)Now;
  AppStartCharging (A);
  AppOnBattery (A, TRUE, TRUE, 3690);
}

// XBL couldn't read the battery's voltage.
STATIC VOID
SChargingUnknown (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  (VOID)Now;
  AppStartCharging (A);
  AppOnBattery (A, TRUE, TRUE, 0);
}

STATIC CONST struct {
  CONST CHAR8  *Name;
  SESSION_SETUP Setup;
} mScreens[] = {
  {"menu", SMenu},
  {"menu-countdown", SCountdown},
  {"menu-no-android", SNoAndroid},
  {"menu-boot-order", SBootOrder},
  {"menu-scrolled", SScrolled},
  {"boot-list", SBootList},
  {"boot-list-sd", SBootListSd},
  {"models", SModels},
  {"confirm-edl", SConfirmEdl},
  {"install", SInstall},
  {"install-replace", SInstallReplace},
  {"install-update", SInstallUpdate},
  {"install-unknown", SInstallUnknown},
  {"install-size", SInstallSize},
  {"confirm-dualboot", SConfirmDualboot},
  {"confirm-update", SConfirmUpdate},
  {"confirm-replace", SConfirmReplace},
  {"installing", SInstalling},
  {"installed", SInstalled},
  {"error-no-card", SErrorNoCard},
  {"error-install", SErrorInstall},
  {"charging", SCharging},
  {"charging-low", SChargingLow},
  {"charging-unknown", SChargingUnknown},
  {"booting", SBooting},
  {"booting-sd", SBootingSd},
  {"booting-android", SBootingAndroid},
  {"error-no-image", SErrorNoImage},
  {"error-pick-gone", SErrorPickGone},
  {"error-pick-failed", SErrorPickFailed},
  {"error-model", SErrorModel},
  {"toast", SToast},
};

STATIC APP mApp;

// Renders mScreens[Screen] to a PNG in Out. With Scanout it also writes
// what the panel receives.
STATIC VOID
Render (
  IN CONST CHAR8 *Out,
  IN INT32        PanelW,
  IN INT32        PanelH,
  IN INT32        Degrees,
  IN UINTN        Screen,
  IN BOOLEAN      Scanout
)
{
  SETTINGS Settings;
  UINT64   Now = 10000;
  CHAR8    Path[512];

  SettingsDefault (&Settings);
  Settings.Rotation = (UINT16)Degrees;
  AsciiStrCopy (Settings.Model, sizeof (Settings.Model), "Retroid Pocket 5");
  Settings.ModelRotation = 270;
  MakeApp (&mApp, &Settings);
  AppStart (&mApp, Now, TRUE);
  mScreens[Screen].Setup (&mApp, Now);
  AppSetPhysicalSize (&mApp, (UINT32)PanelW, (UINT32)PanelH);

  mPw              = PanelW;
  UINTN PixelCount = (UINTN)PanelW * PanelH;
  mLogical         = calloc (PixelCount, 4);
  mPhysical        = calloc (PixelCount, 4);
  PANEL Panel      = {.W       = (UINT32)PanelW,
                      .H       = (UINT32)PanelH,
                      .Present = BlitToPanel,
                      .Now     = Tick,
                      .Logical = mLogical};
  if (!ViewInit (&Panel)) {
    fprintf (stderr, "the view didn't start\n");
    exit (1);
  }
  ViewRotate (AppDesiredRotation (&mApp));
  UINT32 LogicalW;
  UINT32 LogicalH;
  ViewLogicalSize (&LogicalW, &LogicalH);

  ViewSync (&mApp);
  // Let the fades, scrolls and sweeps run a little.
  for (INT32 Index = 0; Index < 12; Index++) {
    mClockMs += 50;
    ViewRun ();
  }
  ViewSettle ();

  snprintf (
    Path, sizeof (Path), "%s/%dx%d-r%d-%s-view.png", Out, PanelW, PanelH,
    Degrees, mScreens[Screen].Name
  );
  WritePng (Path, (INT32)LogicalW, (INT32)LogicalH, mLogical);
  if (Scanout) {
    snprintf (
      Path, sizeof (Path), "%s/%dx%d-r%d-%s-scanout.png", Out, PanelW, PanelH,
      Degrees, mScreens[Screen].Name
    );
    WritePng (Path, PanelW, PanelH, mPhysical);
  }
  ViewDone ();
  free (mLogical);
  free (mPhysical);
}

// --check runs sessions of key presses and platform replies the way the
// main loop does: rotation followed, view synced when the app changes,
// timers every 10 ms. After every step the panel is compared with a full
// redraw. A difference means the screen shows something the app's state
// doesn't explain, such as a leftover where an object was.

// SCAN opens the model list (the scanning screen) and delivers the models a
// moment later. BOOT_ERROR is a boot that found nothing. FRESH compares the
// screen with a view built fresh for the same state.
typedef enum {
  STEP_KEY,
  STEP_FAST,
  STEP_MODELS,
  STEP_LINES,
  STEP_NOTIFY,
  STEP_WAIT,
  STEP_SCAN,
  STEP_BOOT_ERROR,
  STEP_FRESH,
  STEP_ROTATE
} STEP_KIND;

typedef struct _STEP {
  STEP_KIND Kind;
  INPUT     Key;
  INT32     Value; // repeats, WAIT ms or ROTATE degrees
} STEP;

#define PRESS(Key, Count) {STEP_KEY, INPUT_##Key, Count}
// Presses 40 ms apart, like a held key, so each lands mid-animation.
#define PRESS_FAST(Key, Count) {STEP_FAST, INPUT_##Key, Count}
#define ROTATE(Degrees)        {STEP_ROTATE, 0, Degrees}

STATIC CONST STEP mSessionMenu[]   = {PRESS (DOWN, 24),
                                      PRESS (UP, 24),
                                      PRESS (DOWN, 6),
                                      {STEP_WAIT, 0, 400},
                                      PRESS (UP, 6)};
STATIC CONST STEP mSessionModels[] = {
  {STEP_MODELS, 0, 1}, PRESS (DOWN, 4),     PRESS (UP, 4),   PRESS (DOWN, 2),
  PRESS (SELECT, 1),   {STEP_WAIT, 0, 600}, PRESS (DOWN, 3), PRESS (UP, 3)};
STATIC CONST STEP mSessionLines[]   = {{STEP_LINES, 0, 1},
                                       PRESS (DOWN, 30),
                                       PRESS (UP, 30),
                                       PRESS (BACK, 1),
                                       PRESS (DOWN, 2)};
// Install PortareOS's choices and back, then Emergency download mode and
// back.
STATIC CONST STEP mSessionConfirm[] = {
  PRESS (DOWN, 2),   PRESS (SELECT, 1), PRESS (BACK, 1), PRESS (DOWN, 1),
  PRESS (SELECT, 1), PRESS (DOWN, 1),   PRESS (UP, 1),   PRESS (BACK, 1)};
STATIC CONST STEP mSessionToast[] = {{STEP_NOTIFY, 0, 1}, {STEP_WAIT, 0, 3500},
                                     PRESS (DOWN, 3),     {STEP_NOTIFY, 0, 1},
                                     PRESS (UP, 3),       {STEP_WAIT, 0, 3500}};
// Turns the screen through every rotation between presses.
STATIC CONST STEP mSessionRotate[] = {
  ROTATE (90),         {STEP_WAIT, 0, 300}, PRESS (DOWN, 3),
  ROTATE (180),        {STEP_WAIT, 0, 300}, PRESS (UP, 2),
  ROTATE (270),        {STEP_WAIT, 0, 300}, ROTATE (0),
  {STEP_WAIT, 0, 300}, PRESS (DOWN, 3),     PRESS (UP, 6)};

STATIC CONST STEP mSessionHeld[] = {PRESS_FAST (DOWN, 20), {STEP_WAIT, 0, 400},
                                    PRESS_FAST (UP, 20),   {STEP_WAIT, 0, 400},
                                    PRESS_FAST (DOWN, 7),  PRESS_FAST (UP, 3),
                                    PRESS_FAST (DOWN, 9),  {STEP_WAIT, 0, 600}};
STATIC CONST STEP mSessionHeldModels[] = {
  {STEP_MODELS, 0, 1},  PRESS_FAST (DOWN, 5), PRESS_FAST (UP, 5),
  PRESS_FAST (DOWN, 3), PRESS (SELECT, 1),    PRESS_FAST (DOWN, 12),
  PRESS_FAST (UP, 12),  {STEP_WAIT, 0, 600}};
STATIC CONST STEP mSessionHeldLines[] = {
  {STEP_LINES, 0, 1},    PRESS_FAST (DOWN, 25), PRESS_FAST (UP, 25),
  PRESS_FAST (DOWN, 12), PRESS (BACK, 1),       PRESS_FAST (DOWN, 10),
  {STEP_WAIT, 0, 600}};

#define FRESH {STEP_FRESH, 0, 0}
STATIC CONST STEP mSessionPaths[] = {FRESH,
                                     {STEP_SCAN, 0, 1},
                                     FRESH,
                                     PRESS (DOWN, 2),
                                     FRESH,
                                     PRESS (SELECT, 1),
                                     {STEP_WAIT, 0, 600},
                                     FRESH,
                                     {STEP_LINES, 0, 1},
                                     FRESH,
                                     PRESS_FAST (DOWN, 9),
                                     FRESH,
                                     PRESS (BACK, 1),
                                     FRESH,
                                     PRESS (UP, 2),
                                     PRESS (SELECT, 1),
                                     FRESH,
                                     PRESS (BACK, 1),
                                     FRESH,
                                     {STEP_BOOT_ERROR, 0, 1},
                                     FRESH,
                                     PRESS (SELECT, 1),
                                     FRESH,
                                     {STEP_NOTIFY, 0, 1},
                                     {STEP_WAIT, 0, 300},
                                     FRESH,
                                     {STEP_SCAN, 0, 1},
                                     PRESS (BACK, 1),
                                     FRESH,
                                     PRESS (SELECT, 1),
                                     {STEP_WAIT, 0, 300},
                                     FRESH};

// Install PortareOS's screens, each against a fresh view.
STATIC CONST STEP mSessionInstall[] = {
  FRESH,           PRESS (DOWN, 2),   PRESS (SELECT, 1), FRESH,
  PRESS (DOWN, 1), FRESH,             PRESS (UP, 1),     PRESS (SELECT, 1),
  FRESH,           PRESS (DOWN, 3),   FRESH,             PRESS (UP, 2),
  PRESS (SELECT, 1), FRESH,           PRESS (DOWN, 1),   FRESH,
  PRESS (BACK, 1), FRESH,             PRESS (DOWN, 1),   PRESS (SELECT, 1),
  FRESH,           ROTATE (90),       FRESH,             PRESS (BACK, 1),
  FRESH,           PRESS (BACK, 1),   FRESH};

// Rotation changes under every kind of screen.
STATIC CONST STEP mSessionPaths2[] = {FRESH,
                                      PRESS (DOWN, 3),
                                      PRESS (SELECT, 1),
                                      FRESH,
                                      ROTATE (90),
                                      FRESH,
                                      PRESS (BACK, 1),
                                      FRESH,
                                      {STEP_SCAN, 0, 1},
                                      FRESH,
                                      ROTATE (180),
                                      FRESH,
                                      PRESS (DOWN, 2),
                                      FRESH,
                                      {STEP_NOTIFY, 0, 1},
                                      ROTATE (270),
                                      FRESH,
                                      {STEP_LINES, 0, 1},
                                      FRESH,
                                      PRESS_FAST (DOWN, 6),
                                      ROTATE (0),
                                      FRESH,
                                      PRESS (BACK, 1),
                                      FRESH,
                                      ROTATE (90),
                                      FRESH,
                                      {STEP_BOOT_ERROR, 0, 1},
                                      ROTATE (180),
                                      FRESH,
                                      PRESS (SELECT, 1),
                                      ROTATE (270),
                                      FRESH,
                                      PRESS_FAST (DOWN, 12),
                                      FRESH};

STATIC CONST struct {
  CONST CHAR8 *Name;
  CONST STEP  *Steps;
  UINTN        Count;
} mSessions[] = {
#define SESSION(Name, Steps) {Name, Steps, sizeof (Steps) / sizeof (Steps[0])}
  SESSION ("menu", mSessionMenu),
  SESSION ("models", mSessionModels),
  SESSION ("lines", mSessionLines),
  SESSION ("confirm", mSessionConfirm),
  SESSION ("install", mSessionInstall),
  SESSION ("toast", mSessionToast),
  SESSION ("rotate", mSessionRotate),
  SESSION ("held", mSessionHeld),
  SESSION ("held_models", mSessionHeldModels),
  SESSION ("held_lines", mSessionHeldLines),
  SESSION ("paths", mSessionPaths),
  SESSION ("paths2", mSessionPaths2),
#undef SESSION
};

STATIC UINT32 *mSnap;
STATIC INT32   mBadFrames, mWritten;

// Compares mSnap with mPhysical (a full redraw, or with Fresh a fresh
// view). The first 12 mismatches are also saved as PNGs.
STATIC VOID
CompareFrame (
  IN CONST CHAR8 *Out,
  IN CONST CHAR8 *What,
  IN INT32        Frame,
  IN INT32        W,
  IN INT32        H,
  IN BOOLEAN      Fresh
)
{
  UINTN Diff = 0;
  INT32 X0   = W;
  INT32 Y0   = H;
  INT32 X1   = -1;
  INT32 Y1   = -1;

  for (INT32 Y = 0; Y < H; Y++) {
    for (INT32 X = 0; X < W; X++) {
      UINTN Index = (UINTN)Y * W + X;
      if ((mSnap[Index] & 0xffffff) != (mPhysical[Index] & 0xffffff)) {
        Diff++;
        if (X < X0) {
          X0 = X;
        }
        if (Y < Y0) {
          Y0 = Y;
        }
        if (X > X1) {
          X1 = X;
        }
        if (Y > Y1) {
          Y1 = Y;
        }
      }
    }
  }
  if (!Diff) {
    return;
  }
  mBadFrames++;
  printf (
    "  %s %s %d: %zu pixels differ%s in (%d,%d)-(%d,%d)\n", What,
    Fresh ? "at frame" : "frame", Frame, Diff,
    Fresh ? " from a fresh view" : "", X0, Y0, X1, Y1
  );
  if (mWritten < 12) {
    CHAR8 Path[512];
    snprintf (
      Path, sizeof (Path), "%s/check-%02d-%s-%d-%s.png", Out, mWritten, What,
      Frame, Fresh ? "way" : "drawn"
    );
    WritePng (Path, W, H, mSnap);
    snprintf (
      Path, sizeof (Path), "%s/check-%02d-%s-%d-%s.png", Out, mWritten, What,
      Frame, Fresh ? "fresh" : "redrawn"
    );
    WritePng (Path, W, H, mPhysical);
    mWritten++;
  }
}

// Compares the panel with a full redraw.
STATIC VOID
CheckFrame (
  IN CONST CHAR8 *Out,
  IN CONST CHAR8 *What,
  IN INT32        Frame,
  IN INT32        W,
  IN INT32        H
)
{
  // Flush what the view has already asked to redraw. A change it hasn't
  // flushed yet belongs to the next frame and isn't a leftover.
  ViewRedraw (FALSE);
  CopyMem (mSnap, mPhysical, (UINTN)W * H * 4);
  ViewRedraw (TRUE);
  CompareFrame (Out, What, Frame, W, H, FALSE);
}

STATIC PANEL mCheckPanel;

// Compares the screen as the session left it with a view built fresh for
// the same state. The fresh view carries on.
STATIC VOID
FreshCheck (
  IN CONST CHAR8 *Out,
  IN CONST CHAR8 *What,
  IN INT32        Frame,
  IN INT32        W,
  IN INT32        H,
  IN UINT16       Rotation
)
{
  UINTN PixelCount = (UINTN)W * H;

  if (mApp.Dirty) {
    ViewSync (&mApp);
  }
  ViewRun ();
  ViewSettle ();
  ViewRedraw (FALSE);
  CopyMem (mSnap, mPhysical, PixelCount * 4);
  ViewDone ();
  ZeroMem (mPhysical, PixelCount * 4);
  if (!ViewInit (&mCheckPanel)) {
    fprintf (stderr, "the view didn't start again\n");
    exit (1);
  }
  ViewRotate (Rotation);
  ViewSync (&mApp);
  ViewSettle ();
  ViewRedraw (TRUE);
  CompareFrame (Out, What, Frame, W, H, TRUE);
}

// Runs mSessions[Session] and checks every frame. Mismatches go to Out.
STATIC VOID
Check (
  IN CONST CHAR8 *Out,
  IN INT32        PanelW,
  IN INT32        PanelH,
  IN INT32        Degrees,
  IN UINTN        Session
)
{
  STATIC CONST TEXT_LINE Rows[] = {
    {"SoC", "Snapdragon 8 Gen 2"},
    {"Chip", "SM8550 v2.0 \xc2\xb7 id 603"},
    {"RAM", "12 GB (11.2 GiB)"},
    {"Storage", "256 GB"},
    {"SD card", "Not inserted"},
    {"Display", "1080 \xc3\x97 1920"},
    {"Panel", "Retroid Pocket 5 (Visionox)"},
    {"DSI", "qcom,mdss_dsi_visionox_1080p_cmd"},
    {"Android", "14 (UKQ1.230804.001)"},
    {"Model", "Retroid Pocket 5"},
    {"Bootloader", "OpenABL " MENU_VERSION},
    {"UEFI firmware", "Qualcomm 4.0"},
    {"PMIC", "0x0 1.0, 0x1 2.0, 0x2 1.1"},
    {"Storage chip", "UFS \xc2\xb7 SAMSUNG KLUDG4UHGC-B0E1"},
  };
  SETTINGS Settings;
  UINT16   Rotation;
  UINT64   Now   = 10000;
  INT32    Frame = 0;
  CHAR8    What[96];

  SettingsDefault (&Settings);
  Settings.Rotation = (UINT16)Degrees;
  MakeApp (&mApp, &Settings);
  AppStart (&mApp, Now, TRUE);
  AppSetPhysicalSize (&mApp, (UINT32)PanelW, (UINT32)PanelH);
  mPw              = PanelW;
  UINTN PixelCount = (UINTN)PanelW * PanelH;
  mLogical         = calloc (PixelCount, 4);
  mPhysical        = calloc (PixelCount, 4);
  mSnap            = calloc (PixelCount, 4);
  mClockMs         = (UINT32)Now;
  mCheckPanel      = (PANEL){.W       = (UINT32)PanelW,
                             .H       = (UINT32)PanelH,
                             .Present = BlitToPanel,
                             .Now     = Tick,
                             .Logical = mLogical};
  if (!ViewInit (&mCheckPanel)) {
    fprintf (stderr, "the view didn't start\n");
    exit (1);
  }
  Rotation = AppDesiredRotation (&mApp);
  ViewRotate (Rotation);
  mApp.Dirty = TRUE;
  snprintf (
    What, sizeof (What), "%dx%d-r%d-%s", PanelW, PanelH, Degrees,
    mSessions[Session].Name
  );
  for (UINTN Index = 0; Index < mSessions[Session].Count; Index++) {
    CONST STEP *Step = &mSessions[Session].Steps[Index];
    INT32       Repeats =
      Step->Kind == STEP_KEY || Step->Kind == STEP_FAST ? Step->Value : 1;
    INT32 Ms = Step->Kind == STEP_WAIT    ? Step->Value
               : Step->Kind == STEP_FAST  ? 40
               : Step->Kind == STEP_FRESH ? 0
                                          : 300;
    if (Step->Kind == STEP_ROTATE) {
      Repeats = 1;
    }
    if (Step->Kind == STEP_FRESH) {
      FreshCheck (Out, What, Frame, PanelW, PanelH, Rotation);
      continue;
    }
    for (INT32 Repeat = 0; Repeat < Repeats; Repeat++) {
      CONST MODEL *Models;
      UINTN        ModelCount;
      switch (Step->Kind) {
      case STEP_KEY:
      case STEP_FAST:
        AppInput (&mApp, Step->Key);
        break;
      case STEP_MODELS:
        ModelCount = SampleModels (&Models);
        AppOnModels (&mApp, Models, ModelCount);
        break;
      case STEP_LINES:
        AppOnDeviceInfo (&mApp, Rows, sizeof (Rows) / sizeof (Rows[0]));
        break;
      case STEP_NOTIFY:
        AppNotify (&mApp, "Settings saved");
        break;
      case STEP_SCAN:
        AppOpenModels (&mApp);
        for (INT32 Elapsed = 0; Elapsed < 300; Elapsed += 10) {
          mClockMs += 10;
          AppTick (&mApp, mClockMs);
          if (mApp.Dirty) {
            ViewSync (&mApp);
          }
          ViewRun ();
          CheckFrame (Out, What, Frame++, PanelW, PanelH);
        }
        ModelCount = SampleModels (&Models);
        AppOnModels (&mApp, Models, ModelCount);
        break;
      case STEP_BOOT_ERROR:
        AppOnBootResult (&mApp, FALSE, BOOT_ERROR_NO_IMAGE, NULL);
        break;
      case STEP_ROTATE: {
        SETTINGS Changed = mApp.Settings;
        Changed.Rotation = (UINT16)Step->Value;
        AppSetSettings (&mApp, &Changed);
        break;
      }
      case STEP_WAIT:
      case STEP_FRESH:
        break;
      }
      // The main loop, every 10 ms.
      for (INT32 Elapsed = 0; Elapsed < Ms; Elapsed += 10) {
        mClockMs += 10;
        AppTick (&mApp, mClockMs);
        if (AppDesiredRotation (&mApp) != Rotation) {
          Rotation = AppDesiredRotation (&mApp);
          ViewRotate (Rotation);
          mApp.Dirty = TRUE;
        }
        if (mApp.Dirty) {
          ViewSync (&mApp);
        }
        ViewRun ();
        CheckFrame (Out, What, Frame++, PanelW, PanelH);
      }
    }
  }
  ViewDone ();
  free (mLogical);
  free (mPhysical);
  free (mSnap);
}

INT32
main (
  INT32   Argc,
  CHAR8 **Argv
)
{
  CONST CHAR8 *Out = "build/screens", *Filter = "";
  BOOLEAN      DoCheck = FALSE;
  INT32        Count   = 0;

  for (INT32 Index = 1; Index < Argc; Index++) {
    if (!strcmp (Argv[Index], "--out") && Index + 1 < Argc) {
      Out = Argv[++Index];
    } else if (!strcmp (Argv[Index], "--filter") && Index + 1 < Argc) {
      Filter = Argv[++Index];
    } else if (!strcmp (Argv[Index], "--check")) {
      DoCheck = TRUE;
    } else {
      fprintf (
        stderr,
        "usage: %s [--out DIR] [--filter TEXT] [--check]\n",
        Argv[0]
      );
      return 2;
    }
  }
  if (DoCheck) {
    STATIC CONST INT32 Pairs[][3] = {
      {1280, 800, 0}, {1080, 1920, 270}, {960, 1280, 90}, {1080, 1920, 0}};
    for (UINTN Pair = 0; Pair < sizeof (Pairs) / sizeof (Pairs[0]); Pair++) {
      for (UINTN Session = 0;
           Session < sizeof (mSessions) / sizeof (mSessions[0]); Session++) {
        CHAR8 Key[128];
        snprintf (
          Key, sizeof (Key), "%dx%d-r%d-%s", Pairs[Pair][0], Pairs[Pair][1],
          Pairs[Pair][2], mSessions[Session].Name
        );
        if (!strstr (Key, Filter)) {
          continue;
        }
        INT32 Before = mBadFrames;
        Check (Out, Pairs[Pair][0], Pairs[Pair][1], Pairs[Pair][2], Session);
        printf ("%s: %s\n", Key, mBadFrames == Before ? "clean" : "MISMATCH");
        Count++;
      }
    }
    printf (
      "checked %d sessions: %d frame(s) differ from a full redraw\n", Count,
      mBadFrames
    );
    return mBadFrames ? 1 : 0;
  }
  for (UINTN Panel = 0; Panel < sizeof (mPanels) / sizeof (mPanels[0]);
       Panel++) {
    for (UINTN Turn = 0; Turn < 4; Turn++) {
      BOOLEAN Full = FALSE;
      for (UINTN Index = 0; Index < sizeof (mFullSet) / sizeof (mFullSet[0]);
           Index++) {
        Full |= mFullSet[Index][0] == mPanels[Panel][0] &&
                mFullSet[Index][1] == mPanels[Panel][1] &&
                mFullSet[Index][2] == mRotations[Turn];
      }
      for (UINTN Screen = 0; Screen < sizeof (mScreens) / sizeof (mScreens[0]);
           Screen++) {
        CHAR8 Key[128];
        if (Screen != 0 && !Full) {
          continue;
        }
        snprintf (
          Key, sizeof (Key), "%dx%d-r%d-%s", mPanels[Panel][0],
          mPanels[Panel][1], mRotations[Turn], mScreens[Screen].Name
        );
        if (!strstr (Key, Filter)) {
          continue;
        }
        Render (
          Out, mPanels[Panel][0], mPanels[Panel][1], mRotations[Turn], Screen,
          Screen == 0
        );
        Count++;
      }
    }
  }
  printf ("rendered %d screens to %s\n", Count, Out);
  return 0;
}
