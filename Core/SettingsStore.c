/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  SettingsStore - Settings copy in devinfo, for XBL without variable services.
*/

#include "Core/SettingsStore.h"

#include "Base/Buffer.h"
#include "Base/Memory.h"

#define FORMAT    1
#define VALUE_MAX 32

// How a variable's value is encoded in a field.
typedef enum {
  // One byte, from 0 to the field's maximum.
  KIND_BYTE,
  // A 16-bit rotation: 0xFFFF (none or Auto), 0, 90, 180 or 270.
  KIND_ROTATION,
} KIND;

// A small setting and its place in the bitfield.
typedef struct {
  CONST CHAR8 *Key;
  UINT8        At;
  UINT8        Width;
  KIND         Kind;
} FIELD;

STATIC CONST FIELD mFields[] = {
  {SETTINGS_KEY_BOOT_MODE, 0, 2, KIND_BYTE},
  {SETTINGS_KEY_ABL_MODE, 2, 2, KIND_BYTE},
  {SETTINGS_KEY_ALT_VOLUME_UP, 4, 2, KIND_BYTE},
  {SETTINGS_KEY_OFFMODE_CHARGING, 6, 2, KIND_BYTE},
  {SETTINGS_KEY_SHOW_MODEL_MENU, 8, 2, KIND_BYTE},
  {SETTINGS_KEY_CUSTOM_LOGO, 10, 2, KIND_BYTE},
  // Bits 12-13 are unused (they held KeepLogo).
  {SETTINGS_KEY_USB_HOST_LIVE, 14, 2, KIND_BYTE},
  {SETTINGS_KEY_BOOT_SOURCE, 16, 3, KIND_BYTE},
  {SETTINGS_KEY_BOOT_FAILURE, 19, 4, KIND_BYTE},
  {SETTINGS_KEY_DISPLAY_QUIESCE, 23, 4, KIND_BYTE},
  {SETTINGS_KEY_MENU_TIMEOUT, 27, 4, KIND_BYTE},
  {SETTINGS_KEY_ROTATION, 31, 3, KIND_ROTATION},
  {SETTINGS_KEY_MODEL_ROTATION, 34, 3, KIND_ROTATION},
};

#define FIELD_COUNT (sizeof (mFields) / sizeof (*mFields))

_Static_assert (
  FIELD_COUNT + 1 == SETTINGS_STORE_SKIPPED_MAX,
  "skipped"
);
_Static_assert (
  FIELD_COUNT + 3 == SETTINGS_STORE_VARS,
  "vars"
);
_Static_assert (
  VALUE_MAX *SETTINGS_STORE_MODEL_ENTRIES <= SETTINGS_MAX_MODEL_LEN,
  "model"
);

STATIC CONST UINT16 mRotations[] = {0xFFFF, 0, 90, 180, 270};

CONST CHAR8 *CONST gSettingsStoreModelEntries[SETTINGS_STORE_MODEL_ENTRIES] = {
  "OablModel0", "OablModel1", "OablModel2"};

STATIC UINT32
Fnv1a (
  IN CONST UINT8 *Bytes,
  IN UINTN        Size
)
{
  UINT32 Hash = 0x811c9dc5;

  for (UINTN Index = 0; Index < Size; Index++) {
    Hash = (Hash ^ Bytes[Index]) * 0x01000193;
  }
  return Hash;
}

// Encodes a value as its field code, 0 if unset. Fails if it doesn't fit.
STATIC BOOLEAN
FieldCode (
  IN CONST FIELD       *Field,
  IN BOOLEAN            Found,
  IN CONST OABL_BUFFER *Value,
  OUT UINT64           *Code
)
{
  UINT64 Number;

  *Code = 0;
  if (!Found) {
    return TRUE;
  }
  if (Field->Kind == KIND_BYTE && Value->Len == 1) {
    Number = Value->Data[0];
  } else if (Field->Kind == KIND_ROTATION && Value->Len == 2) {
    UINT16 Word = ReadLe16 (Value->Data);

    for (Number = 0; Number < 5 && mRotations[Number] != Word; Number++)
      ;
    if (Number == 5) {
      return FALSE;
    }
  } else {
    return FALSE;
  }
  if (Number + 1 >= 1ull << Field->Width) {
    return FALSE;
  }
  *Code = Number + 1;
  return TRUE;
}

// Decodes a field code into its variable. Code 0 gives an empty value.
STATIC VOID
FieldValue (
  IN CONST FIELD   *Field,
  IN UINT64         Code,
  OUT SETTINGS_VAR *Out
)
{
  Out->Name = Field->Key;
  Out->Len  = 0;
  if (Code == 0) {
    return;
  }
  if (Field->Kind == KIND_BYTE) {
    Out->Value[0] = (UINT8)(Code - 1);
    Out->Len      = 1;
  } else if (Code - 1 < 5) {
    WriteLe16 (Out->Value, mRotations[Code - 1]);
    Out->Len = 2;
  }
}

VOID
SettingsStorePack (
  IN SETTINGS_GET         *Get,
  IN VOID                 *Context OPTIONAL,
  OUT SETTINGS_STORE_VALUE Out[SETTINGS_STORE_ENTRIES],
  OUT CONST CHAR8         *Skipped[SETTINGS_STORE_SKIPPED_MAX],
  OUT UINTN               *SkippedCount
)
{
  SETTINGS_STORE_VALUE *Entry       = &Out[0];
  OABL_BUFFER           Value       = {0};
  UINT64                Bits        = 0;
  UINT32                RotationFor = 0;
  BOOLEAN               Fits;

  *SkippedCount = 0;
  for (UINTN Index = 0; Index < FIELD_COUNT; Index++) {
    UINT64  Code;
    BOOLEAN Found = Get (Context, mFields[Index].Key, &Value);

    if (FieldCode (&mFields[Index], Found, &Value, &Code)) {
      Bits |= Code << mFields[Index].At;
    } else {
      Skipped[(*SkippedCount)++] = mFields[Index].Key;
    }
    BufferFree (&Value);
  }
  *Entry =
    (SETTINGS_STORE_VALUE){.Name = SETTINGS_STORE_ENTRY, .Len = VALUE_MAX};
  Entry->Value[0] = FORMAT;
  if (Get (Context, SETTINGS_KEY_SCHEMA, &Value) && Value.Len) {
    Entry->Value[1] = Value.Data[0];
  }
  BufferFree (&Value);
  WriteLe64 (Entry->Value + 2, Bits);
  if (Get (Context, SETTINGS_KEY_MODEL_ROTATION_FOR, &Value) && Value.Len) {
    RotationFor = Fnv1a (Value.Data, Value.Len);
  }
  BufferFree (&Value);
  WriteLe32 (Entry->Value + 10, RotationFor);
  // Split the model into 32-byte pieces, or store none if it is too long.
  if (!Get (Context, SETTINGS_KEY_MODEL, &Value)) {
    BufferFree (&Value);
  }
  Fits = Value.Len <= VALUE_MAX * SETTINGS_STORE_MODEL_ENTRIES;
  if (!Fits) {
    Skipped[(*SkippedCount)++] = SETTINGS_KEY_MODEL;
  }
  for (UINTN Index = 0; Index < SETTINGS_STORE_MODEL_ENTRIES; Index++) {
    UINTN At = Index * VALUE_MAX;

    Entry  = &Out[1 + Index];
    *Entry = (SETTINGS_STORE_VALUE){.Name = gSettingsStoreModelEntries[Index]};
    Entry->Len = Fits && At < Value.Len ? Value.Len - At : 0;
    Entry->Len = Entry->Len < VALUE_MAX ? Entry->Len : VALUE_MAX;
    for (UINTN Byte = 0; Byte < Entry->Len; Byte++) {
      Entry->Value[Byte] = Value.Data[At + Byte];
    }
  }
  BufferFree (&Value);
}

BOOLEAN
SettingsStoreUnpack (
  IN SETTINGS_STORE_VALUE_OF *ValueOf,
  IN VOID                    *Context OPTIONAL,
  OUT SETTINGS_VAR            Out[SETTINGS_STORE_VARS]
)
{
  OABL_BYTES    Entry;
  OABL_BYTES    Piece;
  SETTINGS_VAR *Model;
  SETTINGS_VAR *RotationFor;
  UINT64        Bits;
  UINT32        Hash;
  UINTN         Count = 0;

  if (!ValueOf (Context, SETTINGS_STORE_ENTRY, &Entry) || Entry.Len < 14 ||
      Entry.Ptr[0] < 1) {
    return FALSE;
  }
  Bits = ReadLe64 (Entry.Ptr + 2);
  Hash = ReadLe32 (Entry.Ptr + 10);
  for (UINTN Index = 0; Index < FIELD_COUNT; Index++) {
    CONST FIELD *Field = &mFields[Index];

    FieldValue (
      Field, (Bits >> Field->At) & ((1ull << Field->Width) - 1), &Out[Count++]
    );
  }
  Out[Count] = (SETTINGS_VAR){.Name = SETTINGS_KEY_SCHEMA};
  if (Entry.Ptr[1]) {
    Out[Count].Value[0] = Entry.Ptr[1];
    Out[Count].Len      = 1;
  }
  Count++;
  Model  = &Out[Count++];
  *Model = (SETTINGS_VAR){.Name = SETTINGS_KEY_MODEL};
  for (UINTN Index = 0; Index < SETTINGS_STORE_MODEL_ENTRIES; Index++) {
    if (!ValueOf (Context, gSettingsStoreModelEntries[Index], &Piece)) {
      continue;
    }
    // devinfo values are at most 32 bytes, so all three pieces fit.
    for (UINTN Byte = 0;
         Byte < Piece.Len && Model->Len < SETTINGS_MAX_MODEL_LEN; Byte++) {
      Model->Value[Model->Len++] = Piece.Ptr[Byte];
    }
  }
  RotationFor  = &Out[Count++];
  *RotationFor = (SETTINGS_VAR){.Name = SETTINGS_KEY_MODEL_ROTATION_FOR};
  if (Hash != 0 && Model->Len && Fnv1a (Model->Value, Model->Len) == Hash) {
    CopyMem (RotationFor->Value, Model->Value, Model->Len);
    RotationFor->Len = Model->Len;
  }
  return TRUE;
}

BOOLEAN
SettingsStoreCovers (IN CONST CHAR8 *Key)
{
  for (UINTN Index = 0; Index < FIELD_COUNT; Index++) {
    if (AsciiStrEq (mFields[Index].Key, Key)) {
      return TRUE;
    }
  }
  return AsciiStrEq (Key, SETTINGS_KEY_SCHEMA) ||
         AsciiStrEq (Key, SETTINGS_KEY_MODEL) ||
         AsciiStrEq (Key, SETTINGS_KEY_MODEL_ROTATION_FOR);
}
