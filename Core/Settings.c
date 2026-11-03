/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Settings - the loader's settings, the rules for changing them, and
  reading and saving them on the device.
*/

#include "Core/Settings.h"

#include "Base/Buffer.h"
#include "Base/Text.h"

// Languages

// Longest stored language, in bytes.
#define MAX_LANGUAGE_LEN 16

#define LANGUAGE_SYMBOL(Id, Symbol) #Symbol,

// Each language's Symbol, in LANG_LIST order, so a LANG_ID indexes it.
STATIC CONST CHAR8 *CONST mLanguageSymbols[SETTINGS_LANGUAGES] = {
  LANG_LIST (LANGUAGE_SYMBOL)};

#define LANGUAGE_SYMBOL_FITS(Id, Symbol) \
  _Static_assert (sizeof (#Symbol) - 1 <= MAX_LANGUAGE_LEN, #Symbol);
LANG_LIST (LANGUAGE_SYMBOL_FITS)

UINT8
SettingsLanguageFromSymbol (
  IN CONST UINT8 *Symbol,
  IN UINTN        Len
)
{
  for (UINT8 Lang = 0; Lang < SETTINGS_LANGUAGES; Lang++) {
    CONST CHAR8 *Have  = mLanguageSymbols[Lang];
    UINTN        Index = 0;

    while (Index < Len && Have[Index] && Have[Index] == (CHAR8)Symbol[Index]) {
      Index++;
    }
    if (Index == Len && Have[Len] == 0) {
      return Lang;
    }
  }
  return SETTINGS_LANGUAGES;
}

// English, wherever LANG_LIST puts it.
STATIC UINT8
LanguageEnglish (VOID)
{
  return SettingsLanguageFromSymbol ((CONST UINT8 *)"En", 2);
}

// Rules

VOID
SettingsDefault (OUT SETTINGS *Settings)
{
  *Settings = (SETTINGS){
    .BootMode      = BOOT_MODE_LINUX,
    .AblMode       = ABL_MODE_SILENT,
    .BootSource    = BOOT_SOURCE_AUTO,
    .Rotation      = ROTATION_AUTO,
    .ModelRotation = ROTATION_NONE,
    .Language      = LanguageEnglish (),
  };
}

VOID
SettingsToggleBootMode (
  IN OUT SETTINGS        *Settings,
  IN CONST SETTINGS_CAPS *Caps
)
{
  if (Settings->BootMode == BOOT_MODE_LINUX) {
    Settings->BootMode = BOOT_MODE_ANDROID;
    return;
  }
  Settings->BootMode = BOOT_MODE_LINUX;
  if (Settings->BootSource == BOOT_SOURCE_INTERNAL && !Caps->CfwOnInternal) {
    Settings->BootSource = BOOT_SOURCE_SD;
  }
}

VOID
SettingsNextBootSource (
  IN OUT SETTINGS        *Settings,
  IN CONST SETTINGS_CAPS *Caps
)
{
  switch (Settings->BootSource) {
  case BOOT_SOURCE_AUTO:
    Settings->BootSource =
      Caps->CfwOnInternal ? BOOT_SOURCE_INTERNAL : BOOT_SOURCE_SD;
    break;
  case BOOT_SOURCE_INTERNAL:
    Settings->BootSource = BOOT_SOURCE_SD;
    break;
  case BOOT_SOURCE_SD:
    Settings->BootSource = Caps->UsbHost ? BOOT_SOURCE_USB : BOOT_SOURCE_AUTO;
    break;
  case BOOT_SOURCE_USB:
    Settings->BootSource = BOOT_SOURCE_AUTO;
    break;
  }
}

BOOLEAN
SettingsBootSourceVisible (
  IN CONST SETTINGS      *Settings,
  IN CONST SETTINGS_CAPS *Caps
)
{
  if (Settings->BootMode == BOOT_MODE_ANDROID) {
    return FALSE;
  }
  return Caps->UsbHost || Caps->CfwOnInternal ||
         Settings->BootSource != BOOT_SOURCE_SD;
}

BOOLEAN
SettingsModelVisible (IN CONST SETTINGS *Settings)
{
  return Settings->BootMode != BOOT_MODE_ANDROID;
}

// TRUE for 0, 90, 180 or 270.
STATIC BOOLEAN
IsQuarterTurn (IN UINT16 Rotation)
{
  return Rotation < 360 && Rotation % 90 == 0;
}

VOID
SettingsNextRotation (IN OUT SETTINGS *Settings)
{
  UINT16 Rotation = Settings->Rotation;

  if (Rotation == ROTATION_AUTO) {
    Settings->Rotation = 0;
  } else if (IsQuarterTurn (Rotation) && Rotation < 270) {
    Settings->Rotation = Rotation + 90;
  } else {
    Settings->Rotation = ROTATION_AUTO;
  }
}

UINT16
SettingsEffectiveRotation (IN CONST SETTINGS *Settings)
{
  if (Settings->Rotation != ROTATION_AUTO) {
    return Settings->Rotation % 360;
  }
  if (Settings->ModelRotation == ROTATION_NONE) {
    return 0;
  }
  return Settings->ModelRotation % 360;
}

#ifdef OABL_UEFI

#include "Base/Memory.h"
#include "Base/Print.h"
#include "Core/SettingsStore.h"
#include "Efi/Abl.h"
#include "Efi/MenuHost.h"
#include "Efi/MigrateModel.h"
#include "Efi/Uninstall.h"

#define STATUS_TEXT(Status) StatusText ((Status), (CHAR8[STATUS_TEXT_SIZE]){0})

STATIC LOG_SINK mLog;

// The store in misc, read once into a static copy that finds and puts
// work on, so nothing allocates once it is read.

STATIC struct {
  // misc was read, and Data is its copy.
  BOOLEAN                Loaded;
  // Data is a valid store: misc's, or an empty one while misc can't be
  // read.
  BOOLEAN                Ready;
  // A put changed Data since the last write.
  BOOLEAN                Dirty;
  // Data is a new store, not yet set up and written.
  BOOLEAN                Fresh;
  // A load failed, and was logged.
  BOOLEAN                Missed;
  EFI_BLOCK_IO_PROTOCOL *BlockIo;
  // The first block of misc's last 4 KiB.
  UINT64                 Lba;
  UINT8                  Data[SETTINGS_STORE_SIZE];
} mStore;

// Finds misc's last 4 KiB. FALSE if there is no single misc or no room.
STATIC BOOLEAN
StoreLocate (VOID)
{
  EFI_BLOCK_IO_PROTOCOL *BlockIo;
  UINT64                 First;
  UINT64                 Last;
  UINTN                  BlockSize;

  if (!UninstallFindMisc (&BlockIo, &First, &Last, mLog)) {
    if (!mStore.Missed) {
      LogLine (mLog, "settings: no single misc partition");
    }
    return FALSE;
  }
  BlockSize = BlockIo->Media->BlockSize;
  if (BlockSize == 0 || BlockSize > SETTINGS_STORE_SIZE ||
      SETTINGS_STORE_SIZE % BlockSize != 0 || Last < First ||
      Last - First + 1 < SETTINGS_STORE_SIZE / BlockSize) {
    if (!mStore.Missed) {
      LogPrint (
        mLog, "settings: misc is %llu blocks of %llu bytes: no room",
        Last < First ? 0 : Last - First + 1, (UINT64)BlockSize
      );
    }
    return FALSE;
  }
  mStore.BlockIo = BlockIo;
  mStore.Lba     = Last + 1 - SETTINGS_STORE_SIZE / BlockSize;
  return TRUE;
}

// Logs a device model carried over into a new store.
STATIC VOID
LogMigratedModel (
  IN CONST UINT8 *Model,
  IN UINTN        Length,
  IN CONST CHAR8 *From
)
{
  CHAR8 Text[SETTINGS_MAX_MODEL_LEN + 1];

  for (UINTN Index = 0; Index < Length; Index++) {
    Text[Index] = Model[Index] ? (CHAR8)Model[Index] : '?';
  }
  Text[Length] = '\0';
  LogPrint (
    mLog, "settings: device model \"%s\" migrated from %s", Text, From
  );
}

// Puts any device model an older loader stored into the copy. FALSE if it
// didn't fit.
STATIC BOOLEAN
StoreMigrateModel (VOID)
{
  UINT8        Model[SETTINGS_MAX_MODEL_LEN];
  UINTN        Length;
  CONST CHAR8 *From;

  if (!MigrateModel (Model, &Length, &From)) {
    return TRUE;
  }
  if (!SettingsStoreSet (mStore.Data, SETTINGS_KEY_MODEL, Model, Length)) {
    return FALSE;
  }
  LogMigratedModel (Model, Length, From);
  return TRUE;
}

// Without misc, an empty store holding any model an older loader stored,
// so the settings can still be changed for this boot. It is never written.
STATIC VOID
StoreUseEmpty (VOID)
{
  if (mStore.Ready) {
    return;
  }
  SettingsStoreFormat (mStore.Data);
  StoreMigrateModel ();
  mStore.Ready = TRUE;
}

// Reads the store from misc if it isn't read yet, replacing anything that
// isn't a valid store, and logs to Log from now on. FALSE if misc can't be
// read; after that only a call with a Log tries again, so quiet loads don't
// rescan the disks.
STATIC BOOLEAN
StoreLoad (IN LOG_SINK Log)
{
  OABL_BUFFER          Raw = {0};
  OABL_BYTES           Model;
  SETTINGS_STORE_STATE State;

  if (Log.Fn) {
    mLog = Log;
  }
  if (mStore.Loaded) {
    return TRUE;
  }
  // Reading misc after a put would lose what was put.
  if ((mStore.Missed && !Log.Fn) || mStore.Dirty) {
    StoreUseEmpty ();
    return FALSE;
  }
  if (!StoreLocate ()) {
    mStore.Missed = TRUE;
    StoreUseEmpty ();
    return FALSE;
  }
  if (!UninstallRead (mStore.BlockIo, mStore.Lba, SETTINGS_STORE_SIZE, &Raw) ||
      Raw.Len != SETTINGS_STORE_SIZE) {
    if (!mStore.Missed) {
      LogLine (mLog, "settings: reading misc failed");
    }
    mStore.Missed = TRUE;
    BufferFree (&Raw);
    StoreUseEmpty ();
    return FALSE;
  }
  CopyMem (mStore.Data, Raw.Data, SETTINGS_STORE_SIZE);
  BufferFree (&Raw);
  State         = SettingsStoreOpen (mStore.Data);
  mStore.Loaded = TRUE;
  mStore.Ready  = TRUE;
  mStore.Fresh  = State != SETTINGS_STORE_FOUND;
  switch (State) {
  case SETTINGS_STORE_FOUND:
    break;
  case SETTINGS_STORE_BLANK:
    LogLine (mLog, "settings: none in misc yet");
    break;
  case SETTINGS_STORE_OTHER_VERSION:
    LogLine (mLog, "settings: misc has another version's settings: replaced");
    if (SettingsStoreGet (mStore.Data, SETTINGS_KEY_MODEL, &Model)) {
      LogMigratedModel (Model.Ptr, Model.Len, "the other version's settings");
    }
    break;
  case SETTINGS_STORE_DAMAGED:
    LogLine (mLog, "settings: misc's store is damaged: replaced");
    break;
  default:
    LogLine (mLog, "settings: the end of misc held other data: replaced");
    break;
  }
  return TRUE;
}

// Finds entry Name. Value points into the copy.
STATIC BOOLEAN
StoreFind (
  IN CONST CHAR8 *Name,
  OUT OABL_BYTES *Value
)
{
  StoreLoad (LOG_QUIET);
  return SettingsStoreGet (mStore.Data, Name, Value);
}

// TRUE if entry Name holds Data. Length 0 matches a missing entry.
STATIC BOOLEAN
StoreHolds (
  IN CONST CHAR8 *Name,
  IN CONST UINT8 *Data OPTIONAL,
  IN UINTN        Length
)
{
  OABL_BYTES Held;
  BOOLEAN    Found = StoreFind (Name, &Held);

  if (Length == 0) {
    return !Found;
  }
  return Found && Held.Len == Length &&
         CompareMem (Held.Ptr, Data, Length) == 0;
}

// Sets entry Name in the copy, without writing it. Length 0 deletes it.
STATIC EFI_STATUS
StorePut (
  IN CONST CHAR8 *Name,
  IN CONST UINT8 *Data OPTIONAL,
  IN UINTN        Length
)
{
  StoreLoad (LOG_QUIET);
  if (!SettingsStoreNameValid (Name)) {
    return EFI_INVALID_PARAMETER;
  }
  if (StoreHolds (Name, Data, Length)) {
    return EFI_SUCCESS;
  }
  if (!SettingsStoreSet (mStore.Data, Name, Data, Length)) {
    return EFI_OUT_OF_RESOURCES;
  }
  mStore.Dirty = TRUE;
  return EFI_SUCCESS;
}

// Writes the copy to misc if a put changed it.
STATIC EFI_STATUS
StoreWrite (VOID)
{
  EFI_STATUS Status;

  if (!mStore.Dirty) {
    return EFI_SUCCESS;
  }
  // Never write over a misc that couldn't be read: it may hold settings.
  if (!mStore.Loaded) {
    return EFI_NOT_FOUND;
  }
  SettingsStoreSeal (mStore.Data);
  Status = UninstallWrite (
    mStore.BlockIo, mStore.Lba, mStore.Data, SETTINGS_STORE_SIZE
  );
  if (Status == EFI_SUCCESS) {
    Status = mStore.BlockIo->FlushBlocks (mStore.BlockIo);
  }
  if (Status != EFI_SUCCESS) {
    LogPrint (mLog, "settings: writing misc failed: %s", STATUS_TEXT (Status));
    return Status;
  }
  mStore.Dirty = FALSE;
  return EFI_SUCCESS;
}

// Writes the copy, and if that fails drops what was put by reading misc
// again, so the settings match misc.
STATIC EFI_STATUS
StoreCommit (VOID)
{
  EFI_STATUS Status = StoreWrite ();

  if (!EFI_ERROR (Status) || !mStore.Loaded || !mStore.Dirty) {
    return Status;
  }
  mStore.Loaded = FALSE;
  mStore.Ready  = FALSE;
  mStore.Dirty  = FALSE;
  LogLine (mLog, "settings: what wasn't written is dropped");
  StoreLoad (mLog);
  return Status;
}

// How each setting is stored

typedef enum {
  // A BOOLEAN or an enum field: one byte, 0 to Max.
  FORMAT_BYTE,
  // Rotation: Le16, ROTATION_AUTO or a quarter turn.
  FORMAT_ROTATION,
  // Model: UTF-8, no NUL.
  FORMAT_MODEL,
  // The Model that ModelRotation was looked up for, once it was.
  FORMAT_MODEL_ROTATION_FOR,
  // ModelRotation: Le16, ROTATION_NONE or degrees clockwise. It counts
  // only for the model it was looked up for.
  FORMAT_MODEL_ROTATION,
  // Language: its Symbol from LANG_LIST.
  FORMAT_LANGUAGE,
} STORED_FORMAT;

typedef struct {
  CONST CHAR8  *Key;
  STORED_FORMAT Format;
  // FORMAT_BYTE only: the field, its size and its largest value.
  UINT16 Offset;
  UINT8  Size;
  UINT8  Max;
} STORED_SETTING;

#define BYTE_SETTING(Key, Field, Max)                                     \
  {SETTINGS_KEY_##Key, FORMAT_BYTE, __builtin_offsetof (SETTINGS, Field), \
   sizeof (((SETTINGS *)0)->Field), (Max)}
#define OTHER_SETTING(Key, Format) {SETTINGS_KEY_##Key, (Format), 0, 0, 0}

// Every setting kept in misc. Model must come before the rotation entries,
// which depend on it.
STATIC CONST STORED_SETTING mStoredSettings[] = {
  BYTE_SETTING (BOOT_MODE, BootMode, BOOT_MODE_ANDROID),
  BYTE_SETTING (ABL_MODE, AblMode, ABL_MODE_VERBOSE),
  BYTE_SETTING (ALT_VOLUME_UP, AltVolumeUp, 1),
  BYTE_SETTING (BOOT_SOURCE, BootSource, BOOT_SOURCE_USB),
  OTHER_SETTING (ROTATION, FORMAT_ROTATION),
  OTHER_SETTING (MODEL, FORMAT_MODEL),
  OTHER_SETTING (MODEL_ROTATION_FOR, FORMAT_MODEL_ROTATION_FOR),
  OTHER_SETTING (MODEL_ROTATION, FORMAT_MODEL_ROTATION),
  BYTE_SETTING (CUSTOM_LOGO, CustomLogo, 1),
  OTHER_SETTING (LANGUAGE, FORMAT_LANGUAGE),
  BYTE_SETTING (USB_HOST_LIVE, UsbHostLive, 1),
  BYTE_SETTING (DISPLAY_OFF, DisplayOff, 1),
  BYTE_SETTING (PANEL_LINES_OFF, PanelLinesOff, 1),
  BYTE_SETTING (BOOT_LOG, BootLog, 1),
  BYTE_SETTING (SD_DIAGNOSTICS, SdDiagnostics, 1),
};

#define STORED_SETTINGS_COUNT \
  (sizeof (mStoredSettings) / sizeof (mStoredSettings[0]))

_Static_assert (sizeof (BOOLEAN) == 1, "BOOLEAN is a byte");
_Static_assert (
  sizeof (BOOT_MODE) == 4 && sizeof (ABL_MODE) == 4 &&
    sizeof (BOOT_SOURCE) == 4,
  "enums are 4 bytes"
);

// A value as stored. Empty deletes the entry.
typedef struct {
  UINT8 Bytes[SETTINGS_MAX_MODEL_LEN];
  UINTN Len;
} STORED_VALUE;

_Static_assert (
  MAX_LANGUAGE_LEN <= SETTINGS_MAX_MODEL_LEN, "a language fits STORED_VALUE"
);

STATIC UINT32
ReadByteField (
  IN CONST STORED_SETTING *Setting,
  IN CONST SETTINGS       *Settings
)
{
  CONST VOID *Field = (CONST UINT8 *)Settings + Setting->Offset;

  return Setting->Size == 1 ? *(CONST UINT8 *)Field : *(CONST UINT32 *)Field;
}

STATIC VOID
WriteByteField (
  IN CONST STORED_SETTING *Setting,
  IN OUT SETTINGS         *Settings,
  IN UINT8                 Value
)
{
  VOID *Field = (UINT8 *)Settings + Setting->Offset;

  if (Setting->Size == 1) {
    *(UINT8 *)Field = Value;
  } else {
    *(UINT32 *)Field = Value;
  }
}

STATIC VOID
SetValueBytes (
  OUT STORED_VALUE *Value,
  IN CONST VOID    *Data,
  IN UINTN          Len
)
{
  CopyMem (Value->Bytes, Data, Len);
  Value->Len = Len;
}

STATIC VOID
SetValueLe16 (
  OUT STORED_VALUE *Value,
  IN UINT16         Word
)
{
  WriteLe16 (Value->Bytes, Word);
  Value->Len = 2;
}

STATIC VOID
EncodeSetting (
  IN CONST STORED_SETTING *Setting,
  IN CONST SETTINGS       *Settings,
  OUT STORED_VALUE        *Value
)
{
  UINTN        ModelLen = AsciiStrLen (Settings->Model);
  CONST CHAR8 *Symbol;

  switch (Setting->Format) {
  case FORMAT_BYTE:
    Value->Bytes[0] = (UINT8)ReadByteField (Setting, Settings);
    Value->Len      = 1;
    break;
  case FORMAT_ROTATION:
    SetValueLe16 (Value, Settings->Rotation);
    break;
  case FORMAT_MODEL:
    SetValueBytes (Value, Settings->Model, ModelLen);
    break;
  case FORMAT_MODEL_ROTATION_FOR:
    SetValueBytes (
      Value, Settings->Model, Settings->ModelRotationKnown ? ModelLen : 0
    );
    break;
  case FORMAT_MODEL_ROTATION:
    SetValueLe16 (Value, Settings->ModelRotation);
    break;
  case FORMAT_LANGUAGE:
    // An unknown language is stored as English.
    Symbol = mLanguageSymbols[Settings->Language < SETTINGS_LANGUAGES
                                ? Settings->Language
                                : LanguageEnglish ()];
    SetValueBytes (Value, Symbol, AsciiStrLen (Symbol));
    break;
  }
}

// A bad stored value leaves the field as it is.
STATIC VOID
DecodeSetting (
  IN CONST STORED_SETTING *Setting,
  IN CONST OABL_BYTES     *Stored,
  IN OUT SETTINGS         *Settings
)
{
  UINT16 Word = Stored->Len == 2 ? ReadLe16 (Stored->Ptr) : 0;
  UINT8  Language;

  switch (Setting->Format) {
  case FORMAT_BYTE:
    if (Stored->Len == 1 && Stored->Ptr[0] <= Setting->Max) {
      WriteByteField (Setting, Settings, Stored->Ptr[0]);
    }
    break;
  case FORMAT_ROTATION:
    if (Stored->Len == 2 && (Word == ROTATION_AUTO || IsQuarterTurn (Word))) {
      Settings->Rotation = Word;
    }
    break;
  case FORMAT_MODEL:
    // Invalid UTF-8 clears the model.
    if (Stored->Len <= SETTINGS_MAX_MODEL_LEN &&
        Utf8Valid (Stored->Ptr, Stored->Len)) {
      CopyMem (Settings->Model, Stored->Ptr, Stored->Len);
      Settings->Model[Stored->Len] = 0;
    }
    break;
  case FORMAT_MODEL_ROTATION_FOR:
    Settings->ModelRotationKnown =
      Settings->Model[0] &&
      TextEq (
        (OABL_TEXT){(CONST CHAR8 *)Stored->Ptr, Stored->Len},
        TextOf (Settings->Model)
      );
    break;
  case FORMAT_MODEL_ROTATION:
    if (Settings->ModelRotationKnown && Stored->Len == 2) {
      Settings->ModelRotation = Word == ROTATION_NONE || Word % 90 != 0
                                  ? ROTATION_NONE
                                  : Word % 360;
    }
    break;
  case FORMAT_LANGUAGE:
    Language = SettingsLanguageFromSymbol (Stored->Ptr, Stored->Len);
    if (Language < SETTINGS_LANGUAGES) {
      Settings->Language = Language;
    }
    break;
  }
}

STATIC BOOLEAN
StoredValuesEqual (
  IN CONST STORED_VALUE *A,
  IN CONST STORED_VALUE *B
)
{
  return A->Len == B->Len &&
         (A->Len == 0 || CompareMem (A->Bytes, B->Bytes, A->Len) == 0);
}

// Formats a stored value for the log.
STATIC VOID
FormatStoredValue (
  IN CONST STORED_SETTING *Setting,
  IN CONST STORED_VALUE   *Value,
  OUT CHAR8               *Out,
  IN UINTN                 OutSize
)
{
  CHAR8 *Text;

  if (Value->Len == 0) {
    AsciiStrCopy (Out, OutSize, "default (deleted)");
    return;
  }
  switch (Setting->Format) {
  case FORMAT_BYTE:
    SPrint (Out, OutSize, "%u", Value->Bytes[0]);
    break;
  case FORMAT_ROTATION:
  case FORMAT_MODEL_ROTATION:
    SPrint (Out, OutSize, "%u", ReadLe16 (Value->Bytes));
    break;
  default:
    Text = Utf8Lossy (Value->Bytes, Value->Len);
    SPrint (Out, OutSize, "\"%s\"", Text ? Text : "");
    FreePool (Text);
    break;
  }
}

// Reading and saving

VOID
SettingsSetUp (IN LOG_SINK Log)
{
  OABL_BYTES Held;
  EFI_STATUS Status;

  if (!StoreLoad (Log) || !mStore.Fresh) {
    return;
  }
  // Write the new store even with no model to carry over, so the next boot
  // finds it.
  mStore.Dirty = TRUE;
  if (!SettingsStoreGet (mStore.Data, SETTINGS_KEY_MODEL, &Held) &&
      !StoreMigrateModel ()) {
    Status = EFI_OUT_OF_RESOURCES;
  } else {
    Status = StoreWrite ();
  }
  if (Status != EFI_SUCCESS) {
    LogPrint (
      mLog, "settings: setting up misc failed: %s", STATUS_TEXT (Status)
    );
    return;
  }
  mStore.Fresh = FALSE;
  LogLine (mLog, "settings: set up in misc");
}

VOID
SettingsPreload (VOID)
{
  StoreLoad (LOG_QUIET);
}

VOID
SettingsLoad (OUT SETTINGS *Out)
{
  OABL_BYTES Stored;

  SettingsDefault (Out);
  for (UINTN Index = 0; Index < STORED_SETTINGS_COUNT; Index++) {
    CONST STORED_SETTING *Setting = &mStoredSettings[Index];

    if (StoreFind (Setting->Key, &Stored)) {
      DecodeSetting (Setting, &Stored, Out);
    }
  }
}

// The settings as stored when the ABL didn't answer after a save, with the
// charger screen and USB host support from Before.
STATIC VOID
LoadWithoutAbl (
  IN CONST MENU_HOST_STATE *Before,
  OUT SETTINGS             *Stored
)
{
  SettingsLoad (Stored);
  Stored->OffmodeCharging = Before->Settings.OffmodeCharging;
  if (Stored->BootSource == BOOT_SOURCE_USB &&
      !(Before->Flags & OPENABL_MENU_STATE_USB_HOST)) {
    Stored->BootSource = BOOT_SOURCE_AUTO;
  }
}

SETTINGS_SAVE_RESULT
SettingsSave (
  IN CONST MENU_HOST *Host,
  IN CONST SETTINGS  *Want,
  OUT SETTINGS       *Stored
)
{
  MENU_HOST_STATE      Before;
  MENU_HOST_STATE      After;
  SETTINGS             Defaults;
  BOOLEAN              AblModeChanged = FALSE;
  BOOLEAN              ChargerChanged = FALSE;
  SETTINGS_SAVE_RESULT Result         = SETTINGS_SAVED;
  CHAR8                Text[SETTINGS_MAX_MODEL_LEN + 8];

  if (EFI_ERROR (MenuHostState (Host, &Before))) {
    return SETTINGS_NO_ABL;
  }
  SettingsDefault (&Defaults);
  for (UINTN Index = 0; Index < STORED_SETTINGS_COUNT; Index++) {
    CONST STORED_SETTING *Setting = &mStoredSettings[Index];
    STORED_VALUE          Value;
    STORED_VALUE          Default;
    EFI_STATUS            Status;

    EncodeSetting (Setting, Want, &Value);
    EncodeSetting (Setting, &Defaults, &Default);
    if (StoredValuesEqual (&Value, &Default)) {
      Value.Len = 0;
    }
    if (StoreHolds (Setting->Key, Value.Bytes, Value.Len)) {
      continue;
    }
    Status = StorePut (Setting->Key, Value.Bytes, Value.Len);
    if (EFI_ERROR (Status)) {
      LogPrint (
        mLog, "setting %s failed: %s", Setting->Key, STATUS_TEXT (Status)
      );
      Result = Setting->Format == FORMAT_MODEL ? SETTINGS_MODEL_NOT_SAVED
                                               : SETTINGS_NOT_SAVED;
      break;
    }
    AblModeChanged = AblModeChanged ||
                     AsciiStrEq (Setting->Key, SETTINGS_KEY_ABL_MODE);
    FormatStoredValue (Setting, &Value, Text, sizeof (Text));
    LogPrint (mLog, "set %s = %s", Setting->Key, Text);
  }
  // One write for all of them, also after a failure, so misc holds what
  // the settings read back.
  if (EFI_ERROR (StoreCommit ()) && Result == SETTINGS_SAVED) {
    Result = SETTINGS_NOT_SAVED;
  }
  // The charger screen is Qualcomm's devinfo flag, set through the ABL.
  if (Result == SETTINGS_SAVED &&
      Want->OffmodeCharging != Before.Settings.OffmodeCharging) {
    EFI_STATUS Status = MenuHostAction (
      Host, Want->OffmodeCharging ? OPENABL_MENU_ACTION_CHARGER_SCREEN_ON
                                  : OPENABL_MENU_ACTION_CHARGER_SCREEN_OFF
    );

    if (EFI_ERROR (Status)) {
      LogPrint (
        mLog, "setting OffmodeCharging failed: %s", STATUS_TEXT (Status)
      );
      Result = SETTINGS_NOT_SAVED;
    } else {
      ChargerChanged = TRUE;
    }
  }
  // Read back what is stored, which also checks the charger screen.
  if (EFI_ERROR (MenuHostState (Host, &After))) {
    LoadWithoutAbl (&Before, Stored);
    if (ChargerChanged) {
      LogLine (mLog, "OffmodeCharging: the ABL didn't answer to check it");
      Result = SETTINGS_NOT_SAVED;
    }
  } else {
    *Stored = After.Settings;
  }
  // Show or hide the ABL's log right away, as AblMode is now stored.
  if (AblModeChanged) {
    if (Stored->AblMode == ABL_MODE_VERBOSE) {
      AblConsoleShow ();
    } else {
      AblConsoleHide ();
    }
  }
  if (ChargerChanged && Result == SETTINGS_SAVED) {
    if (After.Settings.OffmodeCharging != Want->OffmodeCharging) {
      LogLine (mLog, "setting OffmodeCharging failed: devinfo kept it");
      Result = SETTINGS_NOT_SAVED;
    } else {
      LogPrint (
        mLog, "set OffmodeCharging = %u (devinfo)",
        Want->OffmodeCharging ? 1 : 0
      );
    }
  }
  return Result;
}

// One-off flags

BOOLEAN
SettingsFlagGet (
  IN CONST CHAR8  *Name,
  OUT OABL_BUFFER *Out
)
{
  OABL_BYTES Value;

  *Out = (OABL_BUFFER){0};
  return StoreFind (Name, &Value) && BufferFrom (Out, Value.Ptr, Value.Len);
}

BOOLEAN
SettingsFlagIs (
  IN CONST CHAR8 *Name,
  IN UINT8        Value
)
{
  OABL_BYTES Stored;

  return StoreFind (Name, &Stored) && Stored.Len == 1 &&
         Stored.Ptr[0] == Value;
}

EFI_STATUS
SettingsFlagSet (
  IN CONST CHAR8 *Name,
  IN CONST UINT8 *Data OPTIONAL,
  IN UINTN        Size
)
{
  EFI_STATUS Status = StorePut (Name, Data, Size);

  if (EFI_ERROR (Status)) {
    LogPrint (mLog, "setting %s failed: %s", Name, STATUS_TEXT (Status));
    return Status;
  }
  return StoreCommit ();
}

#endif
