/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-source-exception */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/* The ABL's debug output, for the menu program's Log screen. Output from
 * before the program starts is dropped.
 */

#include <Uefi.h>
#include <Library/SerialPortLib.h>
#include <Protocol/OpenAblMenuHost.h>

OPENABL_STORAGE_LOG_SINK gOpenAblLogSink;

RETURN_STATUS
EFIAPI
SerialPortInitialize (VOID)
{
  return RETURN_SUCCESS;
}

UINTN
EFIAPI
SerialPortWrite (IN UINT8 *Buffer, IN UINTN NumberOfBytes)
{
  if (gOpenAblLogSink != NULL) {
    gOpenAblLogSink (Buffer, NumberOfBytes);
  }
  return NumberOfBytes;
}

UINTN
EFIAPI
SerialPortRead (OUT UINT8 *Buffer, IN UINTN NumberOfBytes)
{
  return 0;
}

BOOLEAN
EFIAPI
SerialPortPoll (VOID)
{
  return FALSE;
}

RETURN_STATUS
EFIAPI
SerialPortSetControl (IN UINT32 Control)
{
  return RETURN_UNSUPPORTED;
}

RETURN_STATUS
EFIAPI
SerialPortGetControl (OUT UINT32 *Control)
{
  return RETURN_UNSUPPORTED;
}

RETURN_STATUS
EFIAPI
SerialPortSetAttributes (IN OUT UINT64 *BaudRate,
                         IN OUT UINT32 *ReceiveFifoDepth,
                         IN OUT UINT32 *Timeout,
                         IN OUT EFI_PARITY_TYPE *Parity,
                         IN OUT UINT8 *DataBits,
                         IN OUT EFI_STOP_BITS_TYPE *StopBits)
{
  return RETURN_UNSUPPORTED;
}
