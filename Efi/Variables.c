/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Variables - OpenABL's settings as UEFI non-volatile variables.
*/

#include "Efi/Variables.h"

#include "Base/Memory.h"
#include "Base/Text.h"
#include "Core/DevInfo.h"
#include "Core/Settings.h"
#include "Core/SettingsStore.h"
#include "Efi/Efi.h"
#include "Efi/Qcom.h"

STATIC EFI_GUID mVendor = SETTINGS_VENDOR_GUID;

#define STATUS_TEXT(Status) StatusText ((Status), (CHAR8[STATUS_TEXT_SIZE]){0})

// Converts Name to NUL-terminated UCS-2; FreePool the result.
// EFI_INVALID_PARAMETER for a character outside the BMP,
// EFI_OUT_OF_RESOURCES when out of memory.
STATIC EFI_STATUS
Ucs2 (
  IN CONST CHAR8 *Name,
  OUT CHAR16    **Out
)
{
  UINT16 *Units = Utf16FromStr (Name);

  *Out = NULL;
  if (!Units) {
    return EFI_OUT_OF_RESOURCES;
  }
  for (UINTN Index = 0; Units[Index]; Index++) {
    if (Units[Index] >= 0xD800 && Units[Index] <= 0xDFFF) {
      FreePool (Units);
      return EFI_INVALID_PARAMETER;
    }
  }
  *Out = (CHAR16 *)Units;
  return EFI_SUCCESS;
}

BOOLEAN
VariablesGet (
  IN CONST CHAR8  *Name,
  OUT OABL_BUFFER *Out
)
{
  CHAR16    *VariableName;
  UINT8      Probe;
  UINTN      Size       = 0;
  UINT32     Attributes = 0;
  EFI_STATUS Status;

  *Out = (OABL_BUFFER){0};
  if (Ucs2 (Name, &VariableName) != EFI_SUCCESS) {
    return FALSE;
  }
  // Ask for the size first; it must answer EFI_BUFFER_TOO_SMALL.
  Status =
    gRT->GetVariable (VariableName, &mVendor, &Attributes, &Size, &Probe);
  if (Status != EFI_BUFFER_TOO_SMALL || Size == 0 ||
      !BufferAppendZeros (Out, Size)) {
    FreePool (VariableName);
    return FALSE;
  }
  Status =
    gRT->GetVariable (VariableName, &mVendor, &Attributes, &Size, Out->Data);
  FreePool (VariableName);
  if (Status != EFI_SUCCESS) {
    BufferFree (Out);
    return FALSE;
  }
  BufferTruncate (Out, Size);
  return TRUE;
}

EFI_STATUS
VariablesPut (
  IN CONST CHAR8 *Name,
  IN CONST UINT8 *Data OPTIONAL,
  IN UINTN        Length
)
{
  UINT8      None = 0;
  CHAR16    *VariableName;
  EFI_STATUS Status = Ucs2 (Name, &VariableName);

  if (Status != EFI_SUCCESS) {
    return Status;
  }
  if (Length == 0) {
    Status = gRT->SetVariable (VariableName, &mVendor, 0, 0, &None);
  } else {
    Status = gRT->SetVariable (
      VariableName, &mVendor,
      EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS |
        EFI_VARIABLE_RUNTIME_ACCESS,
      Length, (VOID *)Data
    );
  }
  FreePool (VariableName);
  // Deleting a missing variable counts as success.
  if (Status == EFI_SUCCESS || (Length == 0 && Status == EFI_NOT_FOUND)) {
    return EFI_SUCCESS;
  }
  return Status;
}

// Whether the setting is the single byte Value.
STATIC BOOLEAN
HoldsByte (
  IN CONST CHAR8 *Name,
  IN UINT8        Value
)
{
  OABL_BUFFER Stored;
  BOOLEAN     Holds;

  if (!VariablesGet (Name, &Stored)) {
    return FALSE;
  }
  Holds = Stored.Len == 1 && Stored.Data[0] == Value;
  BufferFree (&Stored);
  return Holds;
}

// Reads devinfo. FALSE without VerifiedBoot or on a read error.
STATIC BOOLEAN
ReadDevinfo (OUT OABL_BUFFER *Raw)
{
  UINT64     Revision;
  EFI_STATUS Status;

  *Raw = (OABL_BUFFER){0};
  if (QcomReadDeviceState (DEVINFO_SIZE, &Revision, &Status, Raw) &&
      Status == EFI_SUCCESS) {
    return TRUE;
  }
  BufferFree (Raw);
  return FALSE;
}

VOID
VariablesChargerScreenToDevinfo (IN LOG_SINK Log)
{
  OABL_BUFFER Value;
  OABL_BUFFER Raw;
  BOOLEAN     On;
  BOOLEAN     Changed;
  EFI_STATUS  Status;

  if (!VariablesGet (SETTINGS_KEY_OFFMODE_CHARGING, &Value)) {
    return;
  }
  if (Value.Len != 1 || Value.Data[0] > 1) {
    BufferFree (&Value);
    return;
  }
  On = Value.Data[0] == 1;
  BufferFree (&Value);
  if (!ReadDevinfo (&Raw)) {
    return;
  }
  if (!DevInfoSetChargerScreen (Raw.Data, Raw.Len, On, &Changed)) {
    LogPrint (
      Log, "settings: the charger screen stays in a variable: devinfo isn't "
           "written yet"
    );
    goto Done;
  }
  if (Changed) {
    if (!QcomWriteDeviceState (Raw.Data, Raw.Len, &Status)) {
      goto Done;
    }
    if (Status != EFI_SUCCESS) {
      LogPrint (
        Log, "settings: the charger screen into devinfo: %s",
        STATUS_TEXT (Status)
      );
      goto Done;
    }
  }
  if (VariablesPut (SETTINGS_KEY_OFFMODE_CHARGING, NULL, 0) == EFI_SUCCESS) {
    QcomFlushVariables (&Status);
  }
  LogPrint (
    Log, "settings: the charger screen is devinfo's: %s", On ? "on" : "off"
  );
Done:
  BufferFree (&Raw);
}

// Clears the settings' old devinfo values in one devinfo write.
STATIC VOID
ClearLegacy (
  IN OUT OABL_BUFFER *Raw,
  IN LOG_SINK         Log
)
{
  CONST CHAR8 *Names[DEVINFO_LEGACY_SETTINGS_LEN + DEVINFO_LEGACY_ONLY_LEN];
  UINTN        Count = 0;
  EFI_STATUS   Status;

  for (UINTN Index = 0; Index < DEVINFO_LEGACY_SETTINGS_LEN; Index++) {
    Names[Count++] = gDevInfoLegacySettings[Index].Name;
  }
  for (UINTN Index = 0; Index < DEVINFO_LEGACY_ONLY_LEN; Index++) {
    Names[Count++] = gDevInfoLegacyOnly[Index];
  }
  if (DevInfoClearValues (Raw->Data, Raw->Len, Names, Count) &&
      QcomWriteDeviceState (Raw->Data, Raw->Len, &Status) &&
      EFI_ERROR (Status)) {
    LogPrint (
      Log, "settings: clearing devinfo's old values: %s", STATUS_TEXT (Status)
    );
  }
}

// Whether Key is one of the settings that were devinfo's persistent values.
STATIC BOOLEAN
LegacyKey (IN CONST CHAR8 *Key)
{
  for (UINTN Index = 0; Index < DEVINFO_LEGACY_SETTINGS_LEN; Index++) {
    if (AsciiStrEq (gDevInfoLegacySettings[Index].Key, Key)) {
      return TRUE;
    }
  }
  return FALSE;
}

VOID
VariablesMoveOutOfDevinfo (IN LOG_SINK Log)
{
  SETTINGS_VAR Vars[DEVINFO_SETTINGS_MOVE_LEN];
  OABL_BUFFER  Raw = {0};
  DEVINFO      Parsed;
  BOOLEAN      HaveRaw = FALSE;
  BOOLEAN      HaveParsed;
  UINT64       Revision;
  UINT64       Moved = 0;
  UINTN        Count;
  EFI_STATUS   Status;

  // An unreadable record isn't a missing one: leave its values for the next
  // boot.
  if (QcomReadDeviceState (DEVINFO_SIZE, &Revision, &Status, &Raw)) {
    if (Status != EFI_SUCCESS) {
      LogPrint (
        Log,
        "settings: devinfo unreadable (%s): the settings stay there for now",
        STATUS_TEXT (Status)
      );
      goto Done;
    }
    HaveRaw = TRUE;
  }
  if (HoldsByte (SETTINGS_KEY_SCHEMA, SETTINGS_SCHEMA)) {
    if (HaveRaw) {
      ClearLegacy (&Raw, Log);
    }
    goto Done;
  }
  HaveParsed = HaveRaw && DevInfoParse (Raw.Data, Raw.Len, &Parsed);
  Count      = DevInfoSettingsMove (HaveParsed ? &Parsed : NULL, Vars);
  for (UINTN Index = 0; Index < Count; Index++) {
    // An empty value means unset; DevInfoLegacySetting never returns one.
    if (Vars[Index].Len && LegacyKey (Vars[Index].Name)) {
      Moved++;
    }
    Status =
      VariablesPut (Vars[Index].Name, Vars[Index].Value, Vars[Index].Len);
    if (Status != EFI_SUCCESS) {
      LogPrint (
        Log, "settings: UEFI variables unavailable (%s); kept in devinfo",
        STATUS_TEXT (Status)
      );
      goto Done;
    }
  }
  QcomFlushVariables (&Status);
  if (!HoldsByte (SETTINGS_KEY_SCHEMA, SETTINGS_SCHEMA)) {
    LogPrint (
      Log,
      "settings: UEFI variables unavailable (read back wrong); kept in devinfo"
    );
    goto Done;
  }
  // Read devinfo again, as the flush may have written the settings' copy.
  if (HaveRaw) {
    BufferFree (&Raw);
    if (ReadDevinfo (&Raw)) {
      ClearLegacy (&Raw, Log);
    }
  }
#ifdef OABL_DEVINFO_SETTINGS
  // Where devinfo stores the settings, write the copy again now that the
  // old values made room. A record an older ABL filled (up to seven of its
  // eight values) had no room for it at the flush above.
  if (!QcomHasVariableServices ()) {
    Status = VariablesPersistToDevinfo ();
    if (EFI_ERROR (Status)) {
      LogPrint (
        Log, "settings: the settings into devinfo: %s", STATUS_TEXT (Status)
      );
    }
  }
#endif
  LogPrint (
    Log, "settings: %llu moved from devinfo to UEFI variables", (UINT64)Moved
  );
Done:
  BufferFree (&Raw);
}

#ifdef OABL_DEVINFO_SETTINGS

// VariablesGet as Core/Settings' SETTINGS_GET.
STATIC BOOLEAN
StoredGet (
  IN VOID            *Context,
  IN CONST CHAR8     *Name,
  IN OUT OABL_BUFFER *Out
)
{
  return VariablesGet (Name, Out);
}

// DevInfoValue of the record at Context, as SETTINGS_STORE_VALUE_OF.
STATIC BOOLEAN
DevinfoValueOf (
  IN VOID        *Context,
  IN CONST CHAR8 *Name,
  OUT OABL_BYTES *Value
)
{
  return DevInfoValue ((CONST DEVINFO *)Context, Name, Value);
}

EFI_STATUS
VariablesPersistToDevinfo (VOID)
{
  SETTINGS_STORE_VALUE Entries[SETTINGS_STORE_ENTRIES];
  CONST CHAR8         *Skipped[SETTINGS_STORE_SKIPPED_MAX];
  UINTN                SkippedCount;
  OABL_BUFFER          Raw;
  BOOLEAN              Fits = TRUE;
  EFI_STATUS           Status;

  if (!ReadDevinfo (&Raw)) {
    return EFI_UNSUPPORTED;
  }
  SettingsStorePack (StoredGet, NULL, Entries, Skipped, &SkippedCount);
  for (UINTN Index = 0; Index < SETTINGS_STORE_ENTRIES; Index++) {
    CONST SETTINGS_STORE_VALUE *Entry = &Entries[Index];

    if (Entry->Len == 0) {
      DevInfoClearValues (Raw.Data, Raw.Len, &Entry->Name, 1);
    } else {
      Fits &= DevInfoSetValue (
        Raw.Data, Raw.Len, Entry->Name, Entry->Value, Entry->Len
      );
    }
  }
  if (!Fits) {
    Status = EFI_OUT_OF_RESOURCES;
  } else if (!QcomWriteDeviceState (Raw.Data, Raw.Len, &Status)) {
    Status = EFI_UNSUPPORTED;
  } else if (Status == EFI_SUCCESS && SkippedCount) {
    Status = EFI_WARN_WRITE_FAILURE;
  }
  BufferFree (&Raw);
  return Status;
}

VOID
VariablesRestoreFromDevinfo (IN LOG_SINK Log)
{
  SETTINGS_VAR Vars[SETTINGS_STORE_VARS];
  OABL_BUFFER  Raw;
  DEVINFO      Parsed;
  UINT64       Set = 0;

  if (QcomHasVariableServices ()) {
    return;
  }
  if (!ReadDevinfo (&Raw)) {
    LogPrint (
      Log, "settings: no variable services, and devinfo unreadable: the "
           "settings are this boot's"
    );
    return;
  }
  if (!DevInfoParse (Raw.Data, Raw.Len, &Parsed)) {
    goto Done;
  }
  if (!SettingsStoreUnpack (DevinfoValueOf, &Parsed, Vars)) {
    LogPrint (
      Log,
      "settings: no variable services: the settings will be kept in devinfo"
    );
    goto Done;
  }
  for (UINTN Index = 0; Index < SETTINGS_STORE_VARS; Index++) {
    if (VariablesPut (Vars[Index].Name, Vars[Index].Value, Vars[Index].Len) ==
          EFI_SUCCESS &&
        Vars[Index].Len) {
      Set++;
    }
  }
  LogPrint (
    Log, "settings: no variable services: %llu setting(s) from devinfo", Set
  );
Done:
  BufferFree (&Raw);
}

#endif
