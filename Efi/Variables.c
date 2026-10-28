/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Variables - OpenABL's settings, kept in the misc partition.
*/

#include "Efi/Variables.h"

#include "Base/Memory.h"
#include "Base/Text.h"
#include "Core/Settings.h"
#include "Core/SettingsStore.h"
#include "Efi/MigrateModel.h"
#include "Efi/Uninstall.h"

#define STATUS_TEXT(Status) StatusText ((Status), (CHAR8[STATUS_TEXT_SIZE]){0})

// The store as read from misc, and where it is. Static, so a peek at
// ExitBootServices needs no allocation.
STATIC struct {
  // misc was read, and Store is its copy.
  BOOLEAN                Loaded;
  // Store is a valid store: misc's, or an empty one while misc can't be
  // read.
  BOOLEAN                Ready;
  // A put changed Store since the last flush.
  BOOLEAN                Dirty;
  // Store is a new store, not yet set up and written.
  BOOLEAN                Fresh;
  // A load failed, and was logged.
  BOOLEAN                Missed;
  EFI_BLOCK_IO_PROTOCOL *BlockIo;
  // The first block of misc's last 4 KiB.
  UINT64                 Lba;
  UINT8                  Store[SETTINGS_STORE_SIZE];
} mStore;

STATIC LOG_SINK mLog;

// The device model of another version's store that was replaced, kept for
// VariablesSetUp.
STATIC UINT8 mKeptModel[SETTINGS_MAX_MODEL_LEN];
STATIC UINTN mKeptModelLength;

// Finds misc's last 4 KiB. FALSE, logging why the first time, if there is
// no single misc or its blocks don't fit the store.
STATIC BOOLEAN
Locate (VOID)
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

// Finds the model to start a new store with: the one kept from another
// version's store, else the one an older loader stored (MigrateModel).
STATIC BOOLEAN
CarriedModel (
  OUT UINT8         Out[SETTINGS_MAX_MODEL_LEN],
  OUT UINTN        *Length,
  OUT CONST CHAR8 **From
)
{
  if (mKeptModelLength) {
    CopyMem (Out, mKeptModel, mKeptModelLength);
    *Length = mKeptModelLength;
    *From   = "the other version's settings in misc";
    return TRUE;
  }
  return MigrateModel (Out, Length, From);
}

// Logs the model CarriedModel found.
STATIC VOID
LogMigrated (
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

// Without misc, an empty store holding any carried model, so the settings
// can still be changed for this boot. Flushes don't write it.
STATIC VOID
Fallback (VOID)
{
  UINT8        Model[SETTINGS_MAX_MODEL_LEN];
  UINTN        Length;
  CONST CHAR8 *From;

  if (mStore.Ready) {
    return;
  }
  SettingsStoreFormat (mStore.Store);
  if (CarriedModel (Model, &Length, &From) &&
      SettingsStoreSet (mStore.Store, SETTINGS_KEY_MODEL, Model, Length)) {
    LogMigrated (Model, Length, From);
  }
  mStore.Ready = TRUE;
}

BOOLEAN
VariablesLoad (IN LOG_SINK Log)
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
  // Only an explicit load looks for misc again after a miss, so the gets
  // that follow don't each scan the disks. Reading misc now would lose
  // what was put since.
  if ((mStore.Missed && !Log.Fn) || mStore.Dirty) {
    Fallback ();
    return FALSE;
  }
  if (!Locate ()) {
    mStore.Missed = TRUE;
    Fallback ();
    return FALSE;
  }
  if (!UninstallRead (mStore.BlockIo, mStore.Lba, SETTINGS_STORE_SIZE, &Raw) ||
      Raw.Len != SETTINGS_STORE_SIZE) {
    if (!mStore.Missed) {
      LogLine (mLog, "settings: reading misc failed");
    }
    mStore.Missed = TRUE;
    BufferFree (&Raw);
    Fallback ();
    return FALSE;
  }
  CopyMem (mStore.Store, Raw.Data, SETTINGS_STORE_SIZE);
  BufferFree (&Raw);
  // Keep the model from the fixed header, which every version keeps,
  // before another version's store is replaced. A model too long to be one
  // is ignored.
  if (!SettingsStoreValid (mStore.Store) &&
      SettingsStoreModel (mStore.Store, &Model) &&
      Model.Len <= SETTINGS_MAX_MODEL_LEN) {
    CopyMem (mKeptModel, Model.Ptr, Model.Len);
    mKeptModelLength = Model.Len;
  }
  State         = SettingsStoreOpen (mStore.Store);
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

BOOLEAN
VariablesGet (
  IN CONST CHAR8  *Name,
  OUT OABL_BUFFER *Out
)
{
  OABL_BYTES Value;

  *Out = (OABL_BUFFER){0};
  VariablesLoad (LOG_QUIET);
  if (!SettingsStoreGet (mStore.Store, Name, &Value)) {
    return FALSE;
  }
  return BufferFrom (Out, Value.Ptr, Value.Len);
}

BOOLEAN
VariablesPeek (
  IN CONST CHAR8 *Name,
  OUT UINT8      *Buffer,
  IN UINTN        Capacity,
  OUT UINTN      *Length
)
{
  OABL_BYTES Value;

  if (!mStore.Ready || !SettingsStoreGet (mStore.Store, Name, &Value) ||
      Value.Len > Capacity) {
    return FALSE;
  }
  CopyMem (Buffer, Value.Ptr, Value.Len);
  *Length = Value.Len;
  return TRUE;
}

EFI_STATUS
VariablesPut (
  IN CONST CHAR8 *Name,
  IN CONST UINT8 *Data OPTIONAL,
  IN UINTN        Length
)
{
  OABL_BYTES Old;
  BOOLEAN    Found;

  VariablesLoad (LOG_QUIET);
  if (!SettingsStoreNameValid (Name)) {
    return EFI_INVALID_PARAMETER;
  }
  Found = SettingsStoreGet (mStore.Store, Name, &Old);
  // Nothing to change, so nothing to write.
  if (Length == 0 ? !Found
                  : Found && Old.Len == Length &&
                      CompareMem (Old.Ptr, Data, Length) == 0) {
    return EFI_SUCCESS;
  }
  if (!SettingsStoreSet (mStore.Store, Name, Data, Length)) {
    return EFI_OUT_OF_RESOURCES;
  }
  mStore.Dirty = TRUE;
  return EFI_SUCCESS;
}

EFI_STATUS
VariablesFlush (VOID)
{
  EFI_STATUS Status;

  if (!mStore.Dirty) {
    return EFI_SUCCESS;
  }
  // Never write over a misc that couldn't be read: it may hold settings.
  if (!mStore.Loaded) {
    return EFI_NOT_FOUND;
  }
  SettingsStoreSeal (mStore.Store);
  Status = UninstallWrite (
    mStore.BlockIo, mStore.Lba, mStore.Store, SETTINGS_STORE_SIZE
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

VOID
VariablesSetUp (IN LOG_SINK Log)
{
  UINT8        Model[SETTINGS_MAX_MODEL_LEN];
  UINTN        Length;
  CONST CHAR8 *From;
  EFI_STATUS   Status = EFI_SUCCESS;

  if (!VariablesLoad (Log) || !mStore.Fresh) {
    return;
  }
  // Write the new store even if there is no model to carry over, so the
  // next boot finds it.
  mStore.Dirty = TRUE;
  if (CarriedModel (Model, &Length, &From)) {
    Status = VariablesPut (SETTINGS_KEY_MODEL, Model, Length);
    if (Status == EFI_SUCCESS) {
      LogMigrated (Model, Length, From);
    }
  }
  if (Status == EFI_SUCCESS) {
    Status = VariablesFlush ();
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
