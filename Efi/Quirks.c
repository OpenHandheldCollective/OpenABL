/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Quirks - live patches for bugs in XBL's drivers, the storage protocol the
  early copy of the menu leaves installed for the ABL, and an override that
  makes XBL's security checks accept every image.
*/

#include "Efi/Quirks.h"

#include "Efi/Efi.h"

#include <Guid/EventGroup.h>
#include <Protocol/Cpu.h>
#include <Protocol/Security.h>
#include <Protocol/Security2.h>

#include "Base/Buffer.h"
#include "Base/Memory.h"
#include "Base/Print.h"
#include "Base/Text.h"
#include "Core/KernelCmdLine.h"
#include "Core/Settings.h"
#include "Efi/Abl.h"
#include "Efi/Dxe.h"
#include "Efi/MenuHost.h"
#include "Efi/NestedDisk.h"
#include "Efi/Qcom.h"
#include "Efi/Uninstall.h"
#include "Efi/UsbHost.h"
#include "Efi/Variables.h"

#ifdef OABL_SYSFONT
#include "Efi/SysFont.h"
#endif

// ChipInfo ids (GetChipId). The SM8250's XBL needs the GPT and FAT quirks.
#define SM8250 356u
#ifdef OABL_SDCC_SM4450
#define SM4450 654u
#endif
#ifdef OABL_SDCC_SM6450
// Snapdragon G2 Gen 1 (TG4070).
#define SM6450 634u
#endif
#ifdef OABL_SDCC_SM7675
// Snapdragon G2 Gen 2 (RP G2).
#define SM7675_G2 700u
#endif

// Event group on which XBL's SdccDxe re-detects the SD card.
STATIC EFI_GUID mDetectSdCard = {
  0xb7972c36,
  0x8a4c,
  0x4a56,
  {0x8b, 0x02, 0x11, 0x59, 0xb5, 0x2d, 0x4b, 0xfb}};

STATIC EFI_GUID mStorageGuid = OPENABL_STORAGE_PROTOCOL_GUID;

// The code signatures are stored XORed with KEY. The scans search the
// firmware's code and loaded images, which include copies of this image, so
// plain signatures would match themselves. HIDE() applies the key at compile
// time; Reveal() undoes it through a volatile the optimiser can't fold.
#define KEY     0x5au
#define HIDE(x) ((UINT8)((x) ^ KEY))

STATIC VOID
Reveal (
  IN CONST UINT8 *Hidden,
  OUT UINT8      *Out,
  IN UINTN        Count
)
{
  volatile UINT8 Key = KEY;

  for (UINTN Index = 0; Index < Count; Index++) {
    Out[Index] = (UINT8)(Hidden[Index] ^ Key);
  }
}

// Bytes whose Mask is 0 match anything.
STATIC BOOLEAN
Matches (
  IN CONST UINT8 *Haystack,
  IN UINTN        HaystackLen,
  IN UINTN        Offset,
  IN CONST UINT8 *Sig,
  IN CONST UINT8 *Mask,
  IN UINTN        Length
)
{
  if (Offset > HaystackLen || Length > HaystackLen - Offset) {
    return FALSE;
  }
  for (UINTN Index = 0; Index < Length; Index++) {
    if (Mask[Index] != 0 && Haystack[Offset + Index] != Sig[Index]) {
      return FALSE;
    }
  }
  return TRUE;
}

// Unaligned reads from a mapped image.
#if defined(OABL_SDCC_SM4450) || defined(OABL_SDCC_SM6450) ||                  \
  defined(OABL_SDCC_SM7675) || defined(OABL_SD_DIAGNOSTICS)
STATIC UINT32
ReadU32 (IN UINT64 Address)
{
  UINT32 Value;

  CopyMem (&Value, (CONST VOID *)(UINTN)Address, sizeof (Value));
  return Value;
}
#endif

STATIC UINT64
ReadU64 (IN UINT64 Address)
{
  UINT64 Value;

  CopyMem (&Value, (CONST VOID *)(UINTN)Address, sizeof (Value));
  return Value;
}

// Patching driver code

#define Q_EFI_MEMORY_WB 0x8ull
#define Q_EFI_MEMORY_RP 0x2000ull
#define Q_EFI_MEMORY_RO 0x20000ull
#define PAGE            4096ull

// Clears the page's write protection. TRUE only if the GCD then reports it
// writable. The CPU call must ask for write-back: XBL ignores any other
// request but still returns EFI_SUCCESS.
STATIC BOOLEAN
MakeWritable (IN UINT64 Addr)
{
  UINT64                          Page = Addr & ~(PAGE - 1);
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR Descriptor;
  EFI_CPU_ARCH_PROTOCOL          *Cpu;

  if (!DxeDescriptor (Page, &Descriptor)) {
    return FALSE;
  }
  DxeSetAttributes (
    Page, PAGE, Descriptor.Attributes & ~(Q_EFI_MEMORY_RO | Q_EFI_MEMORY_RP)
  );
  Cpu = LocateProtocol (&gEfiCpuArchProtocolGuid);
  if (Cpu) {
    Cpu->SetMemoryAttributes (Cpu, Page, PAGE, Q_EFI_MEMORY_WB);
  }
  return DxeDescriptor (Page, &Descriptor) &&
         (Descriptor.Attributes & Q_EFI_MEMORY_RO) == 0;
}

// Makes the new instruction visible to instruction fetch.
STATIC VOID
SyncCode (IN UINT64 Addr)
{
#if defined(__aarch64__)
  __asm__ volatile ("dc civac, %0\n\t"
                    "dsb ish\n\t"
                    "ic ivau, %0\n\t"
                    "dsb ish\n\t"
                    "isb\n\t"
                    :
                    : "r"(Addr)
                    : "memory");
#else
  (VOID)Addr;
#endif
}

// Replaces the word at Addr if it is Orig (any word when !HasOrig). TRUE if
// it reads back as NewWord.
STATIC BOOLEAN
WriteWord (
  IN UINT64  Addr,
  IN BOOLEAN HasOrig,
  IN UINT32  Orig,
  IN UINT32  NewWord
)
{
  volatile UINT32 *Word = (volatile UINT32 *)(UINTN)Addr;

  if ((HasOrig && *Word != Orig) || !MakeWritable (Addr)) {
    return FALSE;
  }
  *Word = NewWord;
  SyncCode (Addr);
  return *Word == NewWord;
}

// Firmware code regions

typedef struct _CODE_REGION {
  UINT64 Start, Len;
} CODE_REGION;
typedef struct _CODE_REGIONS OABL_VEC (CODE_REGION) CODE_REGIONS;

// Returns the boot services, runtime services and loader code regions from
// the memory map. Caller frees the result with VecFree.
STATIC CODE_REGIONS
CodeRegions (VOID)
{
  CODE_REGIONS Out = {0};
  UINTN        Count;
  UINTN        DescriptorSize;
  UINT8       *Map = MemoryMapGet (&Count, &DescriptorSize);

  for (UINTN Index = 0; Map && Index < Count; Index++) {
    CONST EFI_MEMORY_DESCRIPTOR *Descriptor =
      MEMORY_MAP_AT (Map, DescriptorSize, Index);

    if (Descriptor->Type == EfiBootServicesCode ||
        Descriptor->Type == EfiRuntimeServicesCode ||
        Descriptor->Type == EfiLoaderCode) {
      if (!VecPush (
            &Out, ((CODE_REGION){Descriptor->PhysicalStart,
                                 Descriptor->NumberOfPages * PAGE})
          )) {
        break;
      }
    }
  }
  FreePool (Map);
  return Out;
}

STATIC BOOLEAN
ImageRange (
  IN EFI_HANDLE Handle,
  OUT UINT64   *Base,
  OUT UINT64   *Size
)
{
  EFI_LOADED_IMAGE_PROTOCOL *Image =
    GetProtocol (Handle, &gEfiLoadedImageProtocolGuid);

  if (!Image || !Image->ImageBase || Image->ImageSize == 0) {
    return FALSE;
  }
  *Base = (UINT64)(UINTN)Image->ImageBase;
  *Size = Image->ImageSize;
  return TRUE;
}

STATIC BOOLEAN
Inside (
  IN UINT64  Addr,
  IN BOOLEAN Have,
  IN UINT64  Base,
  IN UINT64  Size
)
{
  return Have && Addr >= Base && Addr < Base + Size;
}

STATIC VOID EFIAPI
Nothing (
  IN EFI_EVENT Event,
  IN VOID     *Context
)
{
  (VOID)Event;
  (VOID)Context;
}

// Signals an event group, as the ABL's SignalGuidEvent does.
STATIC VOID
SignalGroup (IN EFI_GUID *Group)
{
  EFI_EVENT Event;

  if (!EFI_ERROR (gBS->CreateEventEx (
        EVT_NOTIFY_SIGNAL, TPL_CALLBACK, Nothing, NULL, Group, &Event
      ))) {
    gBS->SignalEvent (Event);
    gBS->CloseEvent (Event);
  }
}

VOID
QuirksSignalSdDetect (VOID)
{
  SignalGroup (&mDetectSdCard);
}

// SdccDxe: the 1.8 V request

// The movk that sets S18R in the ACMD41 argument and what follows it, in
// the SM8250 and SM4450 builds. Stored XORed (HIDE).
STATIC CONST UINT8 mAcmd41[2][24] = {
  {HIDE (0x15), HIDE (0x00), HIDE (0x90), HIDE (0x52), HIDE (0xf5),
   HIDE (0x3f), HIDE (0xa8), HIDE (0x72), HIDE (0x00), HIDE (0x00),
   HIDE (0x00), HIDE (0x00), HIDE (0xf7), HIDE (0x06), HIDE (0x80),
   HIDE (0x52), HIDE (0x00), HIDE (0x00), HIDE (0x00), HIDE (0x00),
   HIDE (0x39), HIDE (0x05), HIDE (0x80), HIDE (0x52)},
  {HIDE (0x15), HIDE (0x00), HIDE (0x90), HIDE (0x52), HIDE (0xf5),
   HIDE (0x3f), HIDE (0xa8), HIDE (0x72), HIDE (0x00), HIDE (0x00),
   HIDE (0x00), HIDE (0x00), HIDE (0xf6), HIDE (0x03), HIDE (0x1f),
   HIDE (0x2a), HIDE (0x37), HIDE (0x00), HIDE (0x80), HIDE (0x52),
   HIDE (0x38), HIDE (0x05), HIDE (0x80), HIDE (0x52)},
};
STATIC CONST UINT8 mAcmd41Mask[2][24] = {
  {1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1},
  {1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1},
};
#define MOVK_AT         4
#define MOVK_S18R_SET   0x72a83ff5u
#define MOVK_S18R_CLEAR 0x72a81ff5u
// Also sets XPC (OCR bit 28, SDXC power control).
#define MOVK_XPC_S18R_CLEAR 0x72aa1ff5u
// SM8250, SM8550/QCS8550/SG3xG2, SM8650 and SM8750/CQ8725S chip ids, whose
// cards get XPC.
STATIC CONST UINT32 mXpcChips[] = {356, 519, 536, 601, 603, 682, 618, 639, 706};
#define MAX_SITES 16

STATIC BOOLEAN
WantsXpc (IN UINT32 Chip)
{
  for (UINTN Index = 0; Index < sizeof (mXpcChips) / sizeof (*mXpcChips);
       Index++) {
    if (mXpcChips[Index] == Chip) {
      return TRUE;
    }
  }
  return FALSE;
}

typedef struct _U64_LIST
OABL_VEC (UINT64) U64_LIST;

// Finds every ACMD41 site in firmware code outside this image and returns
// the movk addresses. Caller frees the result with VecFree.
STATIC U64_LIST
Acmd41Sites (VOID)
{
  UINT8        Sig0[24];
  UINT8        Sig1[24];
  U64_LIST     Sites   = {0};
  CODE_REGIONS Regions = CodeRegions ();
  UINT64       OwnBase = 0;
  UINT64       OwnSize = 0;
  BOOLEAN      HaveOwn = ImageRange (gImageHandle, &OwnBase, &OwnSize);

  Reveal (mAcmd41[0], Sig0, sizeof (Sig0));
  Reveal (mAcmd41[1], Sig1, sizeof (Sig1));
  for (UINTN Index = 0; Index < Regions.Count; Index++) {
    UINT64       Start = Regions.Items[Index].Start;
    UINT64       Len   = Regions.Items[Index].Len;
    CONST UINT8 *Code  = (CONST UINT8 *)(UINTN)Start;

    // Instructions are word-aligned; unaligned matches are data.
    for (UINTN Offset = 0; Offset + 4 <= Len; Offset += 4) {
      if (Code[Offset] != Sig0[0]) {
        continue;
      }
      if ((Matches (Code, Len, Offset, Sig0, mAcmd41Mask[0], sizeof (Sig0)) ||
           Matches (Code, Len, Offset, Sig1, mAcmd41Mask[1], sizeof (Sig1))) &&
          !Inside (Start + Offset, HaveOwn, OwnBase, OwnSize)) {
        if (!VecPush (&Sites, Start + Offset + MOVK_AT)) {
          break;
        }
      }
    }
  }
  VecFree (&Regions);
  return Sites;
}

// SdccDxe: the card's power-up time

// Every SdccDxe build tries ACMD41 200 times, 5 ms apart, so a card has a
// second to power up, and one still busy then is never found.
#define HIDE32(x)    ((UINT32)((x) ^ 0x5a5a5a5au))
#define SETUP_WORDS  16
#define LOOP_WORDS   32
#define ANY_REGISTER 0x1fu
STATIC CONST UINT32 mCmd8Check = HIDE32 (0x7106a91fu);
STATIC CONST UINT32 mWait5Ms   = HIDE32 (0x52827114u);
STATIC CONST UINT32 mAcmd41Index = HIDE32 (0x52800520u);
STATIC CONST UINT32 mCountDown[2] = {HIDE32 (0x52801916u),
                                     HIDE32 (0x52806416u)};
STATIC CONST UINT32 mCountUp[2][2] = {
  {HIDE32 (0x710322dfu), HIDE32 (0x710c82dfu)},
  {HIDE32 (0x71031edfu), HIDE32 (0x710c7edfu)},
};

STATIC UINT32
Reveal32 (IN UINT32 Hidden)
{
  volatile UINT32 Key = HIDE32 (0);

  return Hidden ^ Key;
}

typedef struct _TRIES_WORD {
  UINT64  At;
  UINT32  Built;
  UINT32  Longer;
  BOOLEAN Patched;
} TRIES_WORD;
typedef struct _TRIES_WORDS
OABL_VEC (TRIES_WORD) TRIES_WORDS;

// Returns the offset of the first of Count words of Code from Offset, ending
// before End, that is Want once the Ignore bits are cleared, or End if none
// is.
STATIC UINTN
FindWord (
  IN CONST UINT32 *Code,
  IN UINTN         End,
  IN UINTN         Offset,
  IN UINTN         Count,
  IN UINT32        Want,
  IN UINT32        Ignore
)
{
  for (UINTN Index = 0; Index < Count && Offset + 4 <= End;
       Index++, Offset += 4) {
    if ((Code[Offset / 4] & ~Ignore) == Want) {
      return Offset;
    }
  }
  return End;
}

// Finds every ACMD41 power up limit in firmware code outside this image.
// Caller frees the result with VecFree.
STATIC TRIES_WORDS
TriesWords (VOID)
{
  TRIES_WORDS  Words   = {0};
  CODE_REGIONS Regions = CodeRegions ();
  UINT64       OwnBase = 0;
  UINT64       OwnSize = 0;
  BOOLEAN      HaveOwn = ImageRange (gImageHandle, &OwnBase, &OwnSize);
  UINT32       Cmd8    = Reveal32 (mCmd8Check);
  UINT32       Wait    = Reveal32 (mWait5Ms);
  UINT32       Acmd41  = Reveal32 (mAcmd41Index);

  for (UINTN Index = 0; Index < Regions.Count; Index++) {
    UINT64        Start = Regions.Items[Index].Start;
    UINTN         Len   = (UINTN)Regions.Items[Index].Len;
    CONST UINT32 *Code  = (CONST UINT32 *)(UINTN)Start;

    for (UINTN Offset = 0; Offset + 4 <= Len; Offset += 4) {
      TRIES_WORD Found = {0};
      UINTN      Delay;
      UINTN      At;

      if (Code[Offset / 4] != Cmd8 ||
          Inside (Start + Offset, HaveOwn, OwnBase, OwnSize)) {
        continue;
      }
      Delay = FindWord (Code, Len, Offset, SETUP_WORDS, Wait, 0);
      if (Delay == Len || FindWord (
                            Code, Delay, Offset, SETUP_WORDS, Acmd41,
                            ANY_REGISTER
                          ) == Delay) {
        continue;
      }

      At = FindWord (
        Code, Delay, Offset, SETUP_WORDS, Reveal32 (mCountDown[0]), 0
      );
      if (At != Delay) {
        Found = (TRIES_WORD){Start + At, Reveal32 (mCountDown[0]),
                             Reveal32 (mCountDown[1]), FALSE};
      }
      for (UINTN Form = 0; Form < 2 && !Found.At; Form++) {
        At = FindWord (
          Code, Len, Delay, LOOP_WORDS, Reveal32 (mCountUp[Form][0]), 0
        );
        if (At != Len) {
          Found = (TRIES_WORD){Start + At, Reveal32 (mCountUp[Form][0]),
                               Reveal32 (mCountUp[Form][1]), FALSE};
        }
      }
      if (Found.At && !VecPush (&Words, Found)) {
        break;
      }
    }
  }
  VecFree (&Regions);
  return Words;
}

// SdccDxe: the bus speed at 3.3 V
#define SPEED_WORDS 5
STATIC CONST UINT32 mSpeedSelect[SPEED_WORDS] = {
  HIDE32 (0xb9400298u), HIDE32 (0x7100171fu), HIDE32 (0x54000001u),
  HIDE32 (0x52800119u), HIDE32 (0x52800076u)};
STATIC CONST UINT32 mSpeedSelectIgnore[SPEED_WORDS] = {0, 0, 0x00ffffe0u, 0,
                                                       0};
STATIC CONST UINT32 mHighSpeed = HIDE32 (0x52800058u);

// Finds every speed selection in firmware code outside this image and
// returns the addresses of its loads. Caller frees the result with VecFree.
STATIC U64_LIST
SpeedSelects (VOID)
{
  U64_LIST     Sites   = {0};
  CODE_REGIONS Regions = CodeRegions ();
  UINT64       OwnBase = 0;
  UINT64       OwnSize = 0;
  BOOLEAN      HaveOwn = ImageRange (gImageHandle, &OwnBase, &OwnSize);
  UINT32       Want[SPEED_WORDS];

  for (UINTN Word = 0; Word < SPEED_WORDS; Word++) {
    Want[Word] = Reveal32 (mSpeedSelect[Word]);
  }
  for (UINTN Index = 0; Index < Regions.Count; Index++) {
    UINT64        Start = Regions.Items[Index].Start;
    UINTN         Len   = (UINTN)Regions.Items[Index].Len;
    CONST UINT32 *Code  = (CONST UINT32 *)(UINTN)Start;

    for (UINTN Offset = 0; Offset + 4 * SPEED_WORDS <= Len; Offset += 4) {
      UINTN Word = 0;

      while (Word < SPEED_WORDS &&
             (Code[Offset / 4 + Word] & ~mSpeedSelectIgnore[Word]) ==
               Want[Word]) {
        Word++;
      }
      if (Word == SPEED_WORDS &&
          !Inside (Start + Offset, HaveOwn, OwnBase, OwnSize) &&
          !VecPush (&Sites, Start + Offset)) {
        break;
      }
    }
  }
  VecFree (&Regions);
  return Sites;
}

#if defined(OABL_SDCC_SM4450) || defined(OABL_SDCC_SM6450) ||                  \
  defined(OABL_SDCC_SM7675)
typedef struct _SDCC_WORD {
  UINT64 Rva;
  UINT32 Orig, Repl;
} SDCC_WORD;
// An SdccDxe build that needs more than the S18R patch.
typedef struct _SDCC_BUILD {
  CONST CHAR8  *Name;
  CONST UINT32 *Chips;
  UINTN         NChips;
  UINT64        ImageSize;
  // Slot table to check before patching.
  BOOLEAN          HasSlots;
  UINT64           SlotsRva;
  CONST UINT32    *Slots;
  UINTN            NSlots;
  CONST SDCC_WORD *Words;
  UINTN            NWords;
} SDCC_BUILD;

#ifdef OABL_SDCC_SM4450
STATIC CONST UINT32    mSm4450Chips[] = {SM4450};
STATIC CONST UINT32    mSm4450Slots[] = {0x007c4000, 0x08804000};
STATIC CONST SDCC_WORD mSm4450Words[] = {{0x527c, 0x360005a8, 0x1400002d},
                                         {0xb1c4, 0xb9400298, 0x52800058}};
#endif
#ifdef OABL_SDCC_SM6450
STATIC CONST UINT32    mSm6450Chips[] = {SM6450};
STATIC CONST UINT32    mSm6450Slots[] = {0x007c4000, 0x08804000};
STATIC CONST SDCC_WORD mSm6450Words[] = {{0x51d8, 0x36000248, 0x14000012},
                                         {0xb08c, 0xb9400298, 0x52800058}};
#endif
#ifdef OABL_SDCC_SM7675
STATIC CONST UINT32    mSm7675Chips[] = {SM7675_G2};
STATIC CONST SDCC_WORD mSm7675Words[] = {{0xadec, 0xb9400298, 0x52800058}};
#endif

// Extra words. VOLT, the SM4450/SM6450's first, turns the tbz on the
// driver's "SD rails already on" byte into a branch to the power-on votes.
// MODE replaces the bus speed mode (MaxSpeedMode, 5 = SDR104) with 2, high
// speed.
STATIC CONST SDCC_BUILD mSdccBuilds[] = {
#ifdef OABL_SDCC_SM4450
  {"sm4450", mSm4450Chips, 1, 0x1e000, TRUE, 0x150f0, mSm4450Slots, 2,
   mSm4450Words, 2},
#endif
#ifdef OABL_SDCC_SM6450
  {"sm6450", mSm6450Chips, 1, 0x1e000, TRUE, 0x14fb8, mSm6450Slots, 2,
   mSm6450Words, 2},
#endif
#ifdef OABL_SDCC_SM7675
  {"sm7675", mSm7675Chips, 1, 0x1e000, FALSE, 0, NULL, 0, mSm7675Words, 1},
#endif
};

// Finds the loaded SdccDxe image matching Build by its PE header and the
// words at their offsets. Returns its base, or 0.
STATIC UINT64
SdccImage (IN CONST SDCC_BUILD *Build)
{
  CODE_REGIONS Regions = CodeRegions ();
  UINT64       Found   = 0;

  for (UINTN Index = 0; Index < Regions.Count && !Found; Index++) {
    UINT64 Start  = Regions.Items[Index].Start;
    UINT64 Len    = Regions.Items[Index].Len;
    UINT64 Offset = 0;

    while (Offset + Build->ImageSize <= Len) {
      UINT64       Base = Start + Offset;
      CONST UINT8 *Mz   = (CONST UINT8 *)(UINTN)Base;
      UINT64       Pe;
      BOOLEAN      All = TRUE;

      Offset += PAGE;
      if (Mz[0] != 'M' || Mz[1] != 'Z') {
        continue;
      }
      Pe = ReadU32 (Base + 0x3c);
      if (Pe < 0x40 || Pe > Build->ImageSize - 0x60 ||
          ReadU32 (Base + Pe) != 0x00004550 ||
          (ReadU32 (Base + Pe + 4) & 0xffff) != 0xaa64 ||
          ReadU32 (Base + Pe + 0x50) != Build->ImageSize) {
        continue;
      }
      for (UINTN Word = 0; Word < Build->NWords; Word++) {
        UINT32 Value = ReadU32 (Base + Build->Words[Word].Rva);

        if (Value != Build->Words[Word].Orig &&
            Value != Build->Words[Word].Repl) {
          All = FALSE;
          break;
        }
      }
      if (All) {
        Found = Base;
        break;
      }
    }
  }
  VecFree (&Regions);
  return Found;
}

STATIC UINT64
SdccBuild (
  IN CONST SDCC_BUILD *Build,
  IN LOG_SINK          Log
)
{
  UINT64 Base = SdccImage (Build);
  UINT32 Done = 0;
  UINT32 All;

  if (!Base) {
    LogPrint (Log, "sdcc %s: SdccDxe image not found", Build->Name);
    return 0;
  }
  if (Build->HasSlots) {
    for (UINTN Index = 0; Index < Build->NSlots; Index++) {
      if (ReadU32 (Base + Build->SlotsRva + 4 * Index) != Build->Slots[Index]) {
        LogPrint (
          Log, "sdcc %s: slot layout unexpected, skipping", Build->Name
        );
        return 0;
      }
    }
  }
  // Bit n: word n is patched. Bit n + 4: the write failed. Bit n + 8: the
  // word is neither the original nor the replacement.
  for (UINTN Index = 0; Index < Build->NWords; Index++) {
    UINT32 Bit   = 1u << Index;
    UINT32 Value = ReadU32 (Base + Build->Words[Index].Rva);

    if (Value == Build->Words[Index].Repl) {
      Done |= Bit;
    } else if (Value != Build->Words[Index].Orig) {
      Done |= Bit << 8;
    } else if (WriteWord (
                 Base + Build->Words[Index].Rva, TRUE, Build->Words[Index].Orig,
                 Build->Words[Index].Repl
               )) {
      Done |= Bit;
    } else {
      Done |= Bit << 4;
    }
  }
  All = (1u << Build->NWords) - 1;
  LogPrint (
    Log, "sdcc %s: img=0x%llx patched=0x%llx (0x%llx = all)", Build->Name,
    (UINT64)Base, (UINT64)Done, (UINT64)All
  );
  return Base;
}
#endif // SDCC builds

// SM4450: a card stuck from power-on

#ifdef OABL_SDCC_SM4450

#define SD_HOST       0x08804000ull
#define SDHCI_PRESENT 0x24u
#define PRESENT_DAT   20u
#define PRESENT_CMD   24u

// TLMM: SDC2's pad control, and its CMD and DATA pull-ups.
#define TLMM_SDC2_PAD 0x0f18f000ull
#define PAD_PULLS     0x1e00u

#define CYCLE_FALL_US 20000u
#define CYCLE_OFF_US  250000u
#define CYCLE_RISE_US 20000u

#define VOLT_RVA      0x527cu
#define VOLT_VOTES    0x1400002du
#define VOLT_NO_VOTES 0x1400003bu

STATIC EFI_GUID mCmdDbGuid = {0x1c34f691,
                              0xd33d,
                              0x4e14,
                              {0xb8, 0xf8, 0x32, 0xc6, 0xc0, 0x29, 0xe9, 0x5b}};
typedef struct _CMD_DB_PROTOCOL CMD_DB_PROTOCOL;
struct _CMD_DB_PROTOCOL {
  UINT64 Revision;
  UINTN  Query;
  EFI_STATUS (EFIAPI *QueryAddr) (
    IN CMD_DB_PROTOCOL *This,
    IN CONST CHAR8     *Name,
    OUT UINT32         *Address
  );
};

STATIC EFI_GUID mRpmhGuid = {0xe1d0b573,
                             0xf29f,
                             0x495d,
                             {0x93, 0x52, 0xca, 0x0d, 0xd4, 0x2a, 0x0a, 0x74}};
#define RPMH_REVISION   0x10001ull
#define RPMH_DRV_HLOS   2u
#define RPMH_SET_ACTIVE 0u
typedef struct _RPMH_PROTOCOL {
  UINT64 Revision;
  UINTN  Init, IsDrvIdValid, TcsNumCmds, CreateHandleInternal,
    CreateHandleExplicit;
  EFI_STATUS (EFIAPI *CreateHandle) (
    IN UINT32       Drv,
    IN CONST CHAR8 *Name,
    OUT VOID      **Handle
  );
  UINTN InvalidateExplicit, RegisterCallback, DeleteHandle, FindResource,
    ChurnAll, ChurnSingle, FinishAmc, FinishSleepWakeAmc, IssueCommandSet,
    InvalidateSleep, InvalidateWake, IssueReadCommand;
  EFI_STATUS (EFIAPI *IssueCommand) (
    IN VOID    *Handle,
    IN UINT32   Set,
    IN BOOLEAN  Completion,
    IN UINT32   Address,
    IN UINT32   Data,
    OUT UINT32 *ReqId
  );
} RPMH_PROTOCOL;
_Static_assert (
  offsetof (
    RPMH_PROTOCOL,
    IssueCommand
  ) == 0x98,
  "EFI_RPMH_PROTOCOL's layout"
);


STATIC CONST CHAR8 *CONST mSdPbs[] = {"vrm.sd", "vrm.px2"};
#define SD_PBS (sizeof (mSdPbs) / sizeof (*mSdPbs))

// TRUE if the SD lines read CMD high, DAT low.
STATIC BOOLEAN
SdStuck (VOID)
{
  UINT32 Present = *(volatile UINT32 *)(UINTN)(SD_HOST + SDHCI_PRESENT);

  return (Present >> PRESENT_CMD & 1) == 1 &&
         (Present >> PRESENT_DAT & 0xf) == 0;
}

STATIC BOOLEAN
SdPbsVote (
  IN RPMH_PROTOCOL *Rpmh,
  IN VOID          *Handle,
  IN CONST UINT32  *Address,
  IN BOOLEAN        On,
  IN LOG_SINK       Log
)
{
  for (UINTN Index = 0; Index < SD_PBS; Index++) {
    UINTN      Pbs    = On ? Index : SD_PBS - 1 - Index;
    UINT32     ReqId  = 0;
    EFI_STATUS Status = Rpmh->IssueCommand (
      Handle, RPMH_SET_ACTIVE, TRUE, Address[Pbs], On ? 1 : 0, &ReqId
    );

    if (EFI_ERROR (Status)) {
      LogPrint (
        Log, "sdcc sm4450: %s %s: %s", mSdPbs[Pbs], On ? "on" : "off",
        StatusText (Status, (CHAR8[STATUS_TEXT_SIZE]){0})
      );
      return FALSE;
    }
  }
  return TRUE;
}

// Power cycle a stuck card
STATIC VOID
SdUnstick (
  IN UINT64   Base,
  IN LOG_SINK Log
)
{
  CMD_DB_PROTOCOL *CmdDb  = LocateProtocol (&mCmdDbGuid);
  RPMH_PROTOCOL   *Rpmh   = LocateProtocol (&mRpmhGuid);
  volatile UINT32 *Pad    = (volatile UINT32 *)(UINTN)TLMM_SDC2_PAD;
  UINT32           Pulled = *Pad;
  VOID            *Handle = NULL;
  UINT32           Address[SD_PBS];
  BOOLEAN          Off;

  LogPrint (Log, "sdcc sm4450: the card holds DAT low, power-cycling it");
  if (!CmdDb || !Rpmh || Rpmh->Revision != RPMH_REVISION ||
      EFI_ERROR (Rpmh->CreateHandle (RPMH_DRV_HLOS, "openabl_sd", &Handle)) ||
      !Handle) {
    LogPrint (Log, "sdcc sm4450: no RPMh client");
    return;
  }
  for (UINTN Pbs = 0; Pbs < SD_PBS; Pbs++) {
    if (EFI_ERROR (CmdDb->QueryAddr (CmdDb, mSdPbs[Pbs], &Address[Pbs])) ||
        !Address[Pbs]) {
      LogPrint (
        Log, "sdcc sm4450: %s not in the command database", mSdPbs[Pbs]
      );
      return;
    }
  }
  Off = SdPbsVote (Rpmh, Handle, Address, FALSE, Log);
  if (Off) {
    gBS->Stall (CYCLE_FALL_US);
    *Pad = Pulled & ~PAD_PULLS;
    gBS->Stall (CYCLE_OFF_US);
  }
  // On whatever went off.
  SdPbsVote (Rpmh, Handle, Address, TRUE, Log);
  gBS->Stall (CYCLE_RISE_US);
  *Pad = Pulled;
  if (!Off) {
    return;
  }
  WriteWord (Base + VOLT_RVA, TRUE, VOLT_VOTES, VOLT_NO_VOTES);
  QuirksSignalSdDetect ();
  WriteWord (Base + VOLT_RVA, TRUE, VOLT_NO_VOTES, VOLT_VOTES);
  LogPrint (
    Log, "sdcc sm4450: after the power cycle the card %s",
    SdStuck () ? "still holds DAT low" : "lets DAT go"
  );
}
#endif

// RP G2: the SD command line pull-up

#ifdef OABL_SDCC_SM7675
// EFI_DTB_EXTN_PROTOCOL (EFIDtbExtn.h), libfdt-style functions over XBL's
// UEFI devicetree. FdtGetNodeHandle is the sixth, after the version.
STATIC EFI_GUID mDtbExtn = {0x0389b776,
                            0x625f,
                            0x11eb,
                            {0x83, 0xbe, 0xc7, 0x41, 0xa9, 0x13, 0xde, 0x34}};
#define FDT_GET_NODE_HANDLE_AT 0x30

// fdt_node_handle (DTBDefs.h).
typedef struct _FDT_NODE {
  CONST UINT8 *Blob;
  INT32        Offset;
} FDT_NODE;
typedef INT32 (EFIAPI *GET_NODE_HANDLE) (
  OUT FDT_NODE   *Node,
  IN CONST UINT8 *Path
);

STATIC UINT32
Be32 (IN UINT64 Address)
{
  return __builtin_bswap32 (ReadU32 (Address));
}

STATIC BOOLEAN
CstrIs (
  IN UINT64       Address,
  IN CONST CHAR8 *Name
)
{
  CONST UINT8 *String = (CONST UINT8 *)(UINTN)Address;
  UINTN        Index  = 0;

  for (; Name[Index]; Index++) {
    if (String[Index] != (UINT8)Name[Index]) {
      return FALSE;
    }
  }
  return String[Index] == 0;
}

// Finds a node's property in place: its value's address and length.
STATIC BOOLEAN
FdtProp (
  IN CONST FDT_NODE *Node,
  IN CONST CHAR8    *Name,
  OUT UINT64        *OutAddr,
  OUT UINT32        *OutLen
)
{
  UINT64 Base = (UINT64)(UINTN)Node->Blob;
  UINT64 Strings;
  UINT64 Pos;
  UINT64 NameLen = 0;

  if (Be32 (Base) != 0xd00dfeed || Node->Offset < 0) {
    return FALSE;
  }
  Strings = Base + Be32 (Base + 12);
  Pos     = Base + Be32 (Base + 8) + (UINT64)Node->Offset;
  if (Be32 (Pos) != 1) {
    return FALSE;
  }
  // Skip the node's name, NUL-terminated and padded to a word.
  Pos += 4;
  while (*(CONST UINT8 *)(UINTN)(Pos + NameLen) != 0) {
    NameLen++;
    if (NameLen > 256) {
      return FALSE;
    }
  }
  Pos += (NameLen + 4) & ~3ull;
  for (;;) {
    switch (Be32 (Pos)) {
    case 3: { // FDT_PROP: length, name offset, value.
      UINT32 Len = Be32 (Pos + 4);

      if (CstrIs (Strings + Be32 (Pos + 8), Name)) {
        *OutAddr = Pos + 12;
        *OutLen  = Len;
        return TRUE;
      }
      Pos += 12 + ((Len + 3u) & ~3u);
      break;
    }
    case 4: // FDT_NOP
      Pos += 4;
      break;
    default:
      return FALSE;
    }
  }
}

// Turns on the G2's SD command line pull-up in XBL's UEFI devicetree by
// giving the command pin's sdc2_active entry the data pins' config word.
// Returns whether it patched.
STATIC BOOLEAN
SdCmdPullup (IN LOG_SINK Log)
{
  CONST UINT8    *Proto = LocateProtocol (&mDtbExtn);
  GET_NODE_HANDLE GetNode;
  FDT_NODE        Active;
  FDT_NODE        Cmd;
  FDT_NODE        Data;
  UINT64          Config;
  UINT64          CmdAt  = 0;
  UINT64          CmdOld = 0;
  UINT32          ConfigLen;
  UINT32          CmdPhandle  = 0;
  UINT32          DataPhandle = 0;
  UINT32          CmdNew      = 0;
  BOOLEAN         HaveActive;
  BOOLEAN         HaveCmd;
  BOOLEAN         HaveData;
  BOOLEAN         HaveCmdWord  = FALSE;
  BOOLEAN         HaveDataWord = FALSE;
  UINT64          PropAddr;
  UINT32          PropLen;

  if (!Proto) {
    LogPrint (
      Log, "sdcc sm7675: no DTB extension protocol, command line left as is"
    );
    return FALSE;
  }
  GetNode = *(CONST GET_NODE_HANDLE *)(Proto + FDT_GET_NODE_HANDLE_AT);
#define NODE(dst, path)                                                        \
  ((dst) = (FDT_NODE){NULL, -1},                                               \
   GetNode (&(dst), (CONST UINT8 *)(path)) == 0 && (dst).Blob != NULL)
  HaveActive = NODE (Active, "/soc/pinctrl@f100000/sdc2_active");
  HaveCmd    = NODE (Cmd, "/soc/pinctrl@f100000/sdc2_cmd");
  HaveData   = NODE (Data, "/soc/pinctrl@f100000/sdc2_data0");
#undef NODE
  if (!HaveActive || !HaveCmd || !HaveData) {
    LogPrint (
      Log, "sdcc sm7675: SD pin nodes not found, command line left as is"
    );
    return FALSE;
  }
  if (!(FdtProp (&Cmd, "phandle", &PropAddr, &PropLen) && PropLen == 4 &&
        (CmdPhandle = Be32 (PropAddr), TRUE)) ||
      !(FdtProp (&Data, "phandle", &PropAddr, &PropLen) && PropLen == 4 &&
        (DataPhandle = Be32 (PropAddr), TRUE)) ||
      !FdtProp (&Active, "config", &Config, &ConfigLen)) {
    LogPrint (
      Log, "sdcc sm7675: SD pin config not found, command line left as is"
    );
    return FALSE;
  }
  for (UINT64 Index = 0; Index < ConfigLen / 8; Index++) {
    UINT64 At      = Config + Index * 8;
    UINT32 Phandle = Be32 (At);

    if (Phandle == CmdPhandle) {
      CmdAt       = At + 4;
      CmdOld      = Be32 (At + 4);
      HaveCmdWord = TRUE;
    } else if (Phandle == DataPhandle) {
      CmdNew       = Be32 (At + 4);
      HaveDataWord = TRUE;
    }
  }
  if (!HaveCmdWord || !HaveDataWord) {
    LogPrint (
      Log, "sdcc sm7675: command or data pin not in sdc2_active, left as is"
    );
    return FALSE;
  }
  if (CmdOld == CmdNew) {
    LogPrint (
      Log, "sdcc sm7675: SD command line already pulled up (0x%llx)",
      (UINT64)CmdOld
    );
    return FALSE;
  }
  BOOLEAN Ok = WriteWord (
    CmdAt, TRUE, __builtin_bswap32 ((UINT32)CmdOld), __builtin_bswap32 (CmdNew)
  );

  LogPrint (
    Log,
    "sdcc sm7675: SD command line pull-up on, as Android has it: config 0x%llx "
    "-> 0x%llx: %s",
    (UINT64)CmdOld, (UINT64)CmdNew, Ok ? "patched" : "REFUSED"
  );
  return Ok;
}
#endif // OABL_SDCC_SM7675

// SdccDxe: the slot state its detection left

#ifdef OABL_SD_DIAGNOSTICS
// The SD card's slot in SdccDxe's table (SDCC_BSP_SLOT_1, SDC2) and the
// table's stride (sdcc_slot_type, 0x100 on the MXF builds).
#define SD_SLOT     1ull
#define SLOT_STRIDE 0x100ull

STATIC UINT64 mSdccSite;

// Finds the base and size of the loaded image containing Addr.
STATIC BOOLEAN
ImageAround (
  IN UINT64   Addr,
  OUT UINT64 *OutBase,
  OUT UINT64 *OutSize
)
{
  CODE_REGIONS Regions = CodeRegions ();
  UINT64       Start   = 0;
  UINT64       Len     = 0;
  UINT64       Pos;
  BOOLEAN      Found = FALSE;

  for (UINTN Index = 0; Index < Regions.Count; Index++) {
    if (Addr >= Regions.Items[Index].Start &&
        Addr < Regions.Items[Index].Start + Regions.Items[Index].Len) {
      Start = Regions.Items[Index].Start;
      Len   = Regions.Items[Index].Len;
      Found = TRUE;
      break;
    }
  }
  VecFree (&Regions);
  if (!Found) {
    return FALSE;
  }
  Pos = Addr & ~(PAGE - 1);
  for (;;) {
    CONST UINT8 *Mz = (CONST UINT8 *)(UINTN)Pos;

    if (Mz[0] == 'M' && Mz[1] == 'Z') {
      UINT64 Pe = ReadU32 (Pos + 0x3c);

      if (Pe >= 0x40 && Pe < 0x1000 && ReadU32 (Pos + Pe) == 0x00004550) {
        UINT64 Size = ReadU32 (Pos + Pe + 0x50);

        if (Addr < Pos + Size && Pos + Size <= Start + Len) {
          *OutBase = Pos;
          *OutSize = Size;
          return TRUE;
        }
      }
    }
    if (Pos < Start + PAGE) {
      return FALSE;
    }
    Pos -= PAGE;
  }
}

// Finds SdccDxe's slot table by decoding the adrp/add in
// sdcc_get_slot_handle.
STATIC BOOLEAN
SlotTable (
  IN UINT64   Base,
  IN UINT64   Size,
  OUT UINT64 *Out
)
{
  for (UINT64 At = Base; At + 24 <= Base + Size; At += 4) {
    if (ReadU32 (At) == 0x7100101f &&
        (ReadU32 (At + 4) & 0xff00001f) == 0x54000002 &&
        (ReadU32 (At + 12) & 0x9f000000) == 0x90000000 &&
        (ReadU32 (At + 16) & 0xffc00000) == 0x91000000 &&
        (ReadU32 (At + 20) & 0xffe0fc1f) == 0x8b002000) {
      UINT64 Adrp  = ReadU32 (At + 12);
      UINT64 Pages = ((Adrp >> 29) & 3) | (((Adrp >> 5) & 0x7ffff) << 2);
      UINT64 Table;

      if (Pages & (1ull << 20)) {
        Pages |= ~((1ull << 21) - 1);
      }
      Table = ((At + 12) & ~(PAGE - 1)) + (Pages << 12) +
              ((ReadU32 (At + 16) >> 10) & 0xfff);
      if (Table >= Base && Table + 4 * SLOT_STRIDE <= Base + Size) {
        *Out = Table;
        return TRUE;
      }
    }
  }
  return FALSE;
}

// SdccDxe's card type codes.
STATIC CONST CHAR8 *
CardName (IN UINT32 Type)
{
  switch (Type) {
  case 0:
    return "none";
  case 1:
    return "SD";
  case 2:
    return "MMC";
  case 5:
    return "SDHC/SDXC";
  case 6:
    return "MMC HC";
  case 7:
    return "UHS-I";
  default:
    return "?";
  }
}

// The common SDCC_STATUS codes (sdcc_errno.h).
STATIC CONST CHAR8 *
SdccError (IN UINT32 Error)
{
  switch (Error) {
  case 0:
    return "none";
  case 1:
    return "unknown";
  case 2:
    return "command timeout";
  case 3:
    return "timeout";
  case 4:
    return "command CRC";
  case 5:
    return "data CRC";
  case 19:
    return "switch";
  case 21:
    return "card undetected";
  case 26:
    return "card init";
  default:
    return "other";
  }
}

// Logs the SD slot's state. Site, an ACMD41 site, locates SdccDxe.
STATIC VOID
SdSlotReport (
  IN UINT64       Site,
  IN CONST CHAR8 *When,
  IN LOG_SINK     Log
)
{
  UINT64       Base;
  UINT64       Size;
  UINT64       Table;
  UINT64       SlotAddr;
  UINT32       Drive;
  UINT32       InUse;
  UINT32       Card;
  UINT32       StateValue;
  UINT32       Error;
  UINT32       Status;
  UINT32       Rca;
  UINT32       Class;
  UINT32       Sectors;
  CONST CHAR8 *State;

  if (!ImageAround (Site, &Base, &Size)) {
    LogPrint (Log, "sdcc: no image around the ACMD41 site");
    return;
  }
  if (!SlotTable (Base, Size, &Table)) {
    LogPrint (Log, "sdcc: SdccDxe's slot table not found");
    return;
  }
  SlotAddr = Table + SD_SLOT * SLOT_STRIDE;
  Drive    = ReadU32 (SlotAddr);
  InUse    = ReadU32 (SlotAddr + 4) & 0xff;
  if (Drive == 0 && InUse == 0) {
    LogPrint (Log, "sdcc: SD slot %s: not opened yet", When);
    return;
  }
  if ((UINT64)Drive != SD_SLOT || InUse > 1) {
    LogPrint (
      Log, "sdcc: SD slot @0x%llx unexpected: drive 0x%llx, in use 0x%llx",
      (UINT64)SlotAddr, (UINT64)Drive, (UINT64)InUse
    );
    return;
  }
  Card       = ReadU32 (SlotAddr + 0x14);
  StateValue = ReadU32 (SlotAddr + 0x18);
  Error      = ReadU32 (SlotAddr + 0x20);
  Status     = ReadU32 (SlotAddr + 0x24);
  Rca        = ReadU32 (SlotAddr + 0x1c) & 0xffff;
  Class      = ReadU32 (SlotAddr + 0x28);
  Sectors    = ReadU32 (SlotAddr + 0x2c);
  State      = StateValue == 0   ? "idle"
               : StateValue == 1 ? "ident"
               : StateValue == 2 ? "ready"
                                 : "?";
  LogPrint (
    Log,
    "sdcc: SD slot %s: card %s, %s, error %llu (%s), status 0x%llx, classed "
    "%s, RCA 0x%llx, %llu "
    "sectors",
    When, CardName (Card), State, (UINT64)Error, SdccError (Error),
    (UINT64)Status, CardName (Class), (UINT64)Rca, (UINT64)Sectors
  );
}
#endif // OABL_SD_DIAGNOSTICS

UINT32
QuirksSdccUhs (
  IN UINT32   Chip,
  IN LOG_SINK Log
)
{
  U64_LIST    Sites   = Acmd41Sites ();
  TRIES_WORDS Tries   = TriesWords ();
  BOOLEAN     Xpc     = WantsXpc (Chip);
  UINT32      Patched = 0;
  UINTN       Longer  = 0;
  BOOLEAN     Pulled  = FALSE;
  BOOLEAN     Detected;
#if defined(OABL_SDCC_SM4450) || defined(OABL_SDCC_SM6450) ||                  \
  defined(OABL_SDCC_SM7675)
  UINT64 Base = 0;
#endif

  if (Sites.Count > MAX_SITES) {
    Sites.Count = MAX_SITES;
  }
  if (Tries.Count > MAX_SITES) {
    Tries.Count = MAX_SITES;
  }
  for (UINTN Index = 0; Index < Sites.Count; Index++) {
    UINT64       Site    = Sites.Items[Index];
    UINT32       NewWord = Xpc ? MOVK_XPC_S18R_CLEAR : MOVK_S18R_CLEAR;
    BOOLEAN      Ok      = WriteWord (Site, TRUE, MOVK_S18R_SET, NewWord);
    CONST CHAR8 *What    = !Ok    ? "REFUSED"
                           : !Xpc ? "patched"
                                  : "patched (S18R clear, XPC set)";

    LogPrint (
      Log, "sdcc: site %llu @0x%llx -> %s", (UINT64)Index, (UINT64)Site, What
    );
    Patched += Ok ? 1 : 0;
  }
  if (Sites.Count == 0) {
    LogPrint (
      Log,
      "sdcc: no ACMD41 site found, signature does not match this firmware build"
    );
  }
  for (UINTN Index = 0; Index < Tries.Count; Index++) {
    TRIES_WORD *Word = &Tries.Items[Index];

    Word->Patched = WriteWord (Word->At, TRUE, Word->Built, Word->Longer);
    LogPrint (
      Log, "sdcc: ACMD41 tries 200 -> 800 @0x%llx: %s", (UINT64)Word->At,
      Word->Patched ? "patched" : "REFUSED"
    );
    Longer += Word->Patched ? 1 : 0;
  }
  if (Tries.Count == 0) {
    LogPrint (Log, "sdcc: no ACMD41 power-up limit found");
  }
  // Only with S18R clear do cards stay at 3.3 V.
  if (Patched > 0) {
    U64_LIST Speeds = SpeedSelects ();

    if (Speeds.Count > MAX_SITES) {
      Speeds.Count = MAX_SITES;
    }
    for (UINTN Index = 0; Index < Speeds.Count; Index++) {
      BOOLEAN Ok = WriteWord (
        Speeds.Items[Index], TRUE, Reveal32 (mSpeedSelect[0]),
        Reveal32 (mHighSpeed)
      );

      LogPrint (
        Log, "sdcc: bus speed @0x%llx: the slot's -> high speed: %s",
        (UINT64)Speeds.Items[Index], Ok ? "patched" : "REFUSED"
      );
    }
    if (Speeds.Count == 0) {
      LogPrint (Log, "sdcc: no bus speed selection found");
    }
    VecFree (&Speeds);
  }
#if defined(OABL_SDCC_SM4450) || defined(OABL_SDCC_SM6450) ||                  \
  defined(OABL_SDCC_SM7675)
  for (UINTN Build = 0; Build < sizeof (mSdccBuilds) / sizeof (*mSdccBuilds);
       Build++) {
    BOOLEAN Match = FALSE;

    for (UINTN Index = 0; Index < mSdccBuilds[Build].NChips; Index++) {
      if (mSdccBuilds[Build].Chips[Index] == Chip) {
        Match = TRUE;
      }
    }
    if (Match) {
      Base = SdccBuild (&mSdccBuilds[Build], Log);
      break;
    }
  }
  // Only the SM4450 build uses the image's base.
  (VOID)Base;
#endif
#ifdef OABL_SD_DIAGNOSTICS
  if (Sites.Count > 0) {
    mSdccSite = Sites.Items[0];
    SdSlotReport (mSdccSite, "before", Log);
  }
#endif
#ifdef OABL_SDCC_SM7675
  Pulled = Chip == SM7675_G2 && SdCmdPullup (Log);
#endif
  Detected = Patched > 0 || Longer > 0 || Pulled;
  if (Detected) {
    // Detection runs inside SignalEvent, as its notify TPL is above ours.
    QuirksSignalSdDetect ();
  }
#ifdef OABL_SDCC_SM4450
  if (Detected && Base && Chip == SM4450 && SdStuck ()) {
    SdUnstick (Base, Log);
  }
#endif
#ifdef OABL_SD_DIAGNOSTICS
  if (Sites.Count > 0) {
    SdSlotReport (mSdccSite, "after", Log);
  }
#endif
  VecFree (&Tries);
  VecFree (&Sites);
  return Patched;
}

// PartitionDxe: the GPT "repair"

typedef struct _GPT_SITE {
  CONST CHAR8 *Name;
  CONST UINT8 *Sig;
  CONST UINT8 *Mask;
  UINTN        Len;
  // Offset of the patched branch from the signature's start.
  UINT64 At;
  UINT32 Orig, Repl;
} GPT_SITE;

// b +0x18 becomes b +0x04, returning NOT_FOUND as upstream does.
STATIC CONST UINT8 mPmbrGate[40] = {
  HIDE (0x1f), HIDE (0x11), HIDE (0x00), HIDE (0xf1), HIDE (0xe1), HIDE (0x00),
  HIDE (0x00), HIDE (0x54), HIDE (0xf9), HIDE (0x03), HIDE (0x13), HIDE (0xaa),
  HIDE (0x06), HIDE (0x00), HIDE (0x00), HIDE (0x14), HIDE (0xe0), HIDE (0x03),
  HIDE (0x14), HIDE (0xaa), HIDE (0x14), HIDE (0x0a), HIDE (0x00), HIDE (0x94),
  HIDE (0xf3), HIDE (0x03), HIDE (0x19), HIDE (0xaa), HIDE (0x54), HIDE (0x02),
  HIDE (0x00), HIDE (0x14), HIDE (0xf9), HIDE (0x03), HIDE (0x1f), HIDE (0xaa),
  HIDE (0x80), HIDE (0x0b), HIDE (0x80), HIDE (0x52)};
STATIC CONST UINT8 mPmbrGateMask[40] = {
  1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1};

// cmp x12, x28; b.ne <repair+write>: the b.ne is retargeted to the
// read-only path.
STATIC CONST UINT8 mAltLba[52] = {
  HIDE (0x64), HIDE (0x3d), HIDE (0x10), HIDE (0x33), HIDE (0x8c), HIDE (0x7c),
  HIDE (0x60), HIDE (0xb3), HIDE (0x9f), HIDE (0x01), HIDE (0x1c), HIDE (0xeb),
  HIDE (0x61), HIDE (0x0a), HIDE (0x00), HIDE (0x54), HIDE (0xe0), HIDE (0x03),
  HIDE (0x18), HIDE (0xaa), HIDE (0xe1), HIDE (0x03), HIDE (0x17), HIDE (0xaa),
  HIDE (0xe2), HIDE (0x03), HIDE (0x1c), HIDE (0xaa), HIDE (0xe3), HIDE (0x03),
  HIDE (0x19), HIDE (0xaa), HIDE (0x3a), HIDE (0x02), HIDE (0x00), HIDE (0x94),
  HIDE (0x0c), HIDE (0x1c), HIDE (0x00), HIDE (0x12), HIDE (0x8c), HIDE (0x14),
  HIDE (0x00), HIDE (0x34), HIDE (0xef), HIDE (0x03), HIDE (0x15), HIDE (0xaa),
  HIDE (0xf0), HIDE (0x03), HIDE (0x19), HIDE (0xaa)};
STATIC CONST UINT8 mAltLbaMask[52] = {
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1};

STATIC CONST GPT_SITE mGptSites[2] = {
  {"pmbr-gate", mPmbrGate, mPmbrGateMask, sizeof (mPmbrGate), 12, 0x14000006,
   0x14000001},
  {"alt-lba", mAltLba, mAltLbaMask, sizeof (mAltLba), 12, 0x54000a61,
   0x54000341},
};

// A site belongs to PartitionDxe if its name is within this distance.
#define NAME_WINDOW 0x20000u

STATIC BOOLEAN mGptDone;

STATIC BOOLEAN
WindowHasName (
  IN CONST UINT8 *Img,
  IN UINTN        Lo,
  IN UINTN        Hi
)
{
  STATIC CONST CHAR8 Name[]  = "PartitionDxe";
  UINTN              NameLen = sizeof (Name) - 1;

  if (Hi < NameLen) {
    return FALSE;
  }
  for (UINTN Index = Lo; Index + NameLen <= Hi; Index++) {
    UINTN Match = 0;

    while (Match < NameLen && Img[Index + Match] == (UINT8)Name[Match]) {
      Match++;
    }
    if (Match == NameLen) {
      return TRUE;
    }
  }
  return FALSE;
}

VOID
QuirksGptClobber (
  IN UINT32   Chip,
  IN LOG_SINK Log
)
{
  EFI_HANDLE  *Images = NULL;
  UINTN        Count;
  UINT64       MeBase = 0;
  UINT64       MeSize = 0;
  BOOLEAN      HaveMe;
  UINT8        Sig0[sizeof (mPmbrGate)];
  UINT8        Sig1[sizeof (mAltLba)];
  CONST UINT8 *Sigs[2]    = {Sig0, Sig1};
  UINT32       Found[2]   = {0, 0};
  UINT32       Patched[2] = {0, 0};

  if (Chip != SM8250 || mGptDone) {
    return;
  }
  mGptDone = TRUE;
  Count    = LocateHandles (&gEfiLoadedImageProtocolGuid, &Images);
  if (!Count) {
    return;
  }
  HaveMe = ImageRange (gImageHandle, &MeBase, &MeSize);
  Reveal (mGptSites[0].Sig, Sig0, mGptSites[0].Len);
  Reveal (mGptSites[1].Sig, Sig1, mGptSites[1].Len);
  for (UINTN Index = 0; Index < Count; Index++) {
    UINT64       Base;
    UINT64       Size;
    CONST UINT8 *Img;

    if (!ImageRange (Images[Index], &Base, &Size)) {
      continue;
    }
    if (HaveMe && Base == MeBase && Size == MeSize) {
      continue;
    }
    Img = (CONST UINT8 *)(UINTN)Base;
    for (UINTN Offset = 0; Offset + 4 <= Size; Offset += 4) {
      for (INT32 Site = 0; Site < 2; Site++) {
        CONST GPT_SITE *Gpt = &mGptSites[Site];
        UINTN           Lo;
        UINTN           Hi;
        UINT64          At;

        if (Img[Offset] != Sigs[Site][0] ||
            !Matches (Img, Size, Offset, Sigs[Site], Gpt->Mask, Gpt->Len)) {
          continue;
        }
        Lo = Offset > NAME_WINDOW ? Offset - NAME_WINDOW : 0;
        Hi = Offset + NAME_WINDOW < Size ? Offset + NAME_WINDOW : Size;
        if (!WindowHasName (Img, Lo, Hi)) {
          continue;
        }
        Found[Site]++;
        At = Base + Offset + Gpt->At;
        if (WriteWord (At, TRUE, Gpt->Orig, Gpt->Repl)) {
          Patched[Site]++;
          LogPrint (Log, "gpt: %s patched @0x%llx", Gpt->Name, (UINT64)At);
        }
      }
    }
  }
  gBS->FreePool (Images);
  for (INT32 Site = 0; Site < 2; Site++) {
    LogPrint (
      Log, "gpt: %s sites=%llu patched=%llu", mGptSites[Site].Name,
      (UINT64)Found[Site], (UINT64)Patched[Site]
    );
  }
}

// FAT: the FAT cache that runs into the data cache

// EnhancedFatDxe's FAT_VOLUME: signature, VolumeInterface offset and the
// fields read.
#define FAT_VOLUME_SIGNATURE 0x76746166ull // 'f','a','t','v' little-endian
#define VOLUME_INTERFACE_AT  0x18
#define VOLUME_SIZE          0x48
#define FAT_POS              0x50
#define ROOT_POS             0x58
#define FAT_SIZE             0x68
// DISK_CACHE fields, header and tag sizes, and how far into the volume to
// look for the caches.
#define DC_BASE       0x00
#define DC_LIMIT      0x08
#define DC_GROUP_MASK 0x20
#define DC_HEADER     0x28
#define DC_TAG        0x18
#define SCAN_LIMIT    0x2000

VOID
QuirksFatCache (
  IN UINT32   Chip,
  IN LOG_SINK Log
)
{
  EFI_HANDLE *Handles = NULL;
  UINTN       Count;

  if (Chip != SM8250) {
    return;
  }
  Count = LocateHandles (&gEfiSimpleFileSystemProtocolGuid, &Handles);
  for (UINTN Index = 0; Index < Count; Index++) {
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs =
      GetProtocol (Handles[Index], &gEfiSimpleFileSystemProtocolGuid);
    UINT64  V;
    UINT64  VolSize;
    UINT64  FatPos;
    UINT64  RootPos;
    UINT64  FatSize;
    UINT64  FatDc  = 0;
    UINT64  DataDc = 0;
    UINT64  Offset;
    UINT64  Stride;
    UINT64  FatMask;
    UINT64  DataMask;
    BOOLEAN HaveFat  = FALSE;
    BOOLEAN HaveData = FALSE;
    BOOLEAN Layout;

    if (!Fs) {
      continue;
    }
    V = (UINT64)(UINTN)Fs - VOLUME_INTERFACE_AT;
    if (ReadU64 (V) != FAT_VOLUME_SIGNATURE) {
      continue;
    }
    VolSize = ReadU64 (V + VOLUME_SIZE);
    FatPos  = ReadU64 (V + FAT_POS);
    RootPos = ReadU64 (V + ROOT_POS);
    FatSize = ReadU64 (V + FAT_SIZE);
    if (FatPos == 0 || FatSize == 0 || RootPos <= FatPos ||
        VolSize <= RootPos) {
      continue;
    }
    Offset = 0x70;
    while (Offset + DC_GROUP_MASK + 8 <= SCAN_LIMIT) {
      UINT64 CacheBase  = ReadU64 (V + Offset + DC_BASE);
      UINT64 CacheLimit = ReadU64 (V + Offset + DC_LIMIT);

      if (!HaveFat && CacheBase == FatPos && CacheLimit == FatPos + FatSize) {
        FatDc   = V + Offset;
        HaveFat = TRUE;
      }
      if (!HaveData && CacheBase == RootPos && CacheLimit == VolSize) {
        DataDc   = V + Offset;
        HaveData = TRUE;
      }
      Offset += 8;
    }
    if (!HaveFat || !HaveData || DataDc <= FatDc) {
      continue;
    }
    Stride   = DataDc - FatDc;
    FatMask  = ReadU64 (FatDc + DC_GROUP_MASK);
    DataMask = ReadU64 (DataDc + DC_GROUP_MASK);
    Layout   = Stride > DC_HEADER && (Stride - DC_HEADER) % DC_TAG == 0 &&
               DataMask == (Stride - DC_HEADER) / DC_TAG - 1 &&
               (DataMask & (DataMask + 1)) == 0 && FatMask < 0x10000 &&
               (FatMask & (FatMask + 1)) == 0;
    if (!Layout) {
      LogPrint (
        Log,
        "fat cache: layout unconfirmed (stride 0x%llx FatMask %llu DataMask "
        "%llu), "
        "skipping; the raw FAT reader will do",
        (UINT64)Stride, (UINT64)FatMask, (UINT64)DataMask
      );
    } else if (FatMask > DataMask) {
      LogPrint (
        Log, "fat cache: capped FAT GroupMask %llu -> %llu (volume 0x%llx)",
        (UINT64)FatMask, (UINT64)DataMask, (UINT64)V
      );
      // The layout was confirmed above, so this is the FAT cache's mask.
      *(volatile UINT64 *)(UINTN)(FatDc + DC_GROUP_MASK) = DataMask;
    }
  }
  if (Handles) {
    gBS->FreePool (Handles);
  }
}

VOID
QuirksConnectAll (
  IN UINT32   Chip,
  IN LOG_SINK Log
)
{
  QuirksGptClobber (Chip, Log);
  InventoryConnectAll ();
  NestedDiskPublish (Log);
  QuirksFatCache (Chip, Log);
}

// SdccDxe's DetectCard only opens the SD slot if its ClientHandle is NULL.
// Once a card has been detected, removing or swapping it while in the menu
// does not clear ClientHandle until a BlockIo transfer runs
// sdcc_handle_is_mem_present (CMD13), sees the card is gone or reset, and
// calls sdcc_handle_close. Issue a zero-byte ReadBlocks on each removable raw
// BlockIo handle before QuirksSignalSdDetect so a removed or swapped card's
// stale handle is closed first.
STATIC VOID
SdccRefreshRemovableSlots (VOID)
{
  EFI_HANDLE *Handles = NULL;
  UINTN       Count   = LocateHandles (&gEfiBlockIoProtocolGuid, &Handles);

  for (UINTN Index = 0; Index < Count; Index++) {
    EFI_BLOCK_IO_PROTOCOL *Bio = NULL;
    UINT8                  Dummy;

    if (EFI_ERROR (gBS->HandleProtocol (
          Handles[Index], &gEfiBlockIoProtocolGuid, (VOID **)&Bio
        )) ||
        !Bio || !Bio->Media || !Bio->Media->RemovableMedia ||
        Bio->Media->LogicalPartition || !Bio->Media->MediaPresent ||
        !Bio->ReadBlocks) {
      continue;
    }
    Bio->ReadBlocks (Bio, Bio->Media->MediaId, 0, 0, &Dummy);
  }
  if (Handles) {
    gBS->FreePool (Handles);
  }
}

VOID
QuirksPrepareStorage (
  IN UINT32   Chip,
  IN LOG_SINK Log
)
{
  SdccRefreshRemovableSlots ();
  QuirksSignalSdDetect ();
#ifdef OABL_SD_DIAGNOSTICS
  if (mSdccSite != 0) {
    SdSlotReport (mSdccSite, "prepare", Log);
  }
#endif
  QuirksConnectAll (Chip, Log);
}

UINT32
QuirksChipId (VOID)
{
  QCOM_CHIP Chip;

  if (!QcomChip (&Chip)) {
    return 0;
  }
  return Chip.Id.Status == EFI_SUCCESS ? Chip.Id.Value : 0;
}

// The storage protocol

STATIC UINT32 mInstalledChip;
// Entry point for Run, or NULL.
STATIC OPENABL_STORAGE_RUN mRunFn;

VOID
QuirksSetRun (IN OPENABL_STORAGE_RUN Run)
{
  mRunFn = Run;
}

VOID
QuirksToAbl (
  IN OPENABL_STORAGE_LOG Log OPTIONAL,
  IN CONST CHAR8        *Line
)
{
  CHAR8 *Prefixed;

  if (!Log) {
    return;
  }
  Prefixed = SPrintAlloc ("openabl-menu: %s", Line);
  if (Prefixed) {
    Log ((CONST CHAR8 *)Prefixed);
    FreePool (Prefixed);
  }
}

// LOG_SINK callback. Context carries the ABL's log function.
STATIC VOID
AblLogSink (
  IN VOID        *Context OPTIONAL,
  IN CONST CHAR8 *Line
)
{
  QuirksToAbl ((OPENABL_STORAGE_LOG)(UINTN)Context, Line);
}

LOG_SINK
QuirksAblLog (IN OPENABL_STORAGE_LOG Log OPTIONAL)
{
  return (LOG_SINK){AblLogSink, (VOID *)(UINTN)Log};
}

// Copies Bytes and a NUL to Out, following UEFI's size convention:
// EFI_BUFFER_TOO_SMALL with *Size set when Out is too small.
STATIC EFI_STATUS
Give (
  IN CONST UINT8 *Bytes,
  IN UINTN        Len,
  OUT CHAR8      *Out OPTIONAL,
  IN OUT UINTN   *Size
)
{
  UINTN Need = Len + 1;

  if (!Out || *Size < Need) {
    *Size = Need;
    return EFI_BUFFER_TOO_SMALL;
  }
  for (UINTN Index = 0; Index < Len; Index++) {
    Out[Index] = (CHAR8)Bytes[Index];
  }
  Out[Len] = 0;
  *Size    = Need;
  return EFI_SUCCESS;
}

// Appends the OEM androidboot ids as "key\0value\0" pairs.
STATIC VOID
AndroidParams (
  IN LOG_SINK         Log,
  IN OUT OABL_BUFFER *Out
)
{
  KERNEL_CMDLINE_PARAM                Params[KERNEL_CMDLINE_ANDROID_PARAMS];
  CHAR8                              *Panel = QcomDisplayPanelConfig ();
  EFI_PLATFORMINFO_PLATFORM_INFO_TYPE Platform;
  QCOM_CHIP                           Chip;
  BOOLEAN                             Sm8750;
  UINTN                               Count;

  (VOID)QcomPlatform (&Platform);
  // Only the SM8750 stock ABL passes the display id.
  Sm8750 = QcomChip (&Chip) && TextStartsWith (TextOf (Chip.Name), "SM8750");
  Count  = KernelCmdLineAndroidParams (
    Panel ? Panel : "", Platform.subtype, (UINT8)Platform.fusion, Sm8750, Params
  );

  for (UINTN Index = 0; Index < Count; Index++) {
    LogPrint (
      Log, "android parameter:%s%s", Params[Index].Key, Params[Index].Value
    );
    BufferAppend (Out, Params[Index].Key, AsciiStrLen (Params[Index].Key));
    BufferAppendU8 (Out, 0);
    BufferAppend (Out, Params[Index].Value, AsciiStrLen (Params[Index].Value));
    BufferAppendU8 (Out, 0);
  }
  FreePool (Panel);
}

STATIC EFI_STATUS EFIAPI
PAndroidParams (
  IN OPENABL_STORAGE_PROTOCOL *This,
  OUT CHAR8                   *Out OPTIONAL,
  IN OUT UINTN                *Size,
  IN OPENABL_STORAGE_LOG       Log OPTIONAL
)
{
  OABL_BUFFER Buffer = {0};
  EFI_STATUS  Status;

  (VOID)This;
  if (!Size) {
    return EFI_INVALID_PARAMETER;
  }
  AndroidParams (QuirksAblLog (Log), &Buffer);
  if (Buffer.Len == 0) {
    BufferFree (&Buffer);
    return EFI_NOT_FOUND;
  }
  Status = Give (Buffer.Data, Buffer.Len, Out, Size);
  BufferFree (&Buffer);
  return Status;
}

// Reads the one-byte setting Key. FALSE if it isn't one byte.
STATIC BOOLEAN
SettingByte (
  IN CONST CHAR8 *Key,
  OUT UINT8      *Out
)
{
  OABL_BUFFER Buffer = {0};
  BOOLEAN     Ok     = FALSE;

  if (VariablesGet (Key, &Buffer)) {
    if (Buffer.Len == 1) {
      *Out = Buffer.Data[0];
      Ok   = TRUE;
    }
    BufferFree (&Buffer);
  }
  return Ok;
}

// Returns 1 if the one-byte setting Key is set and nonzero, else 0.
STATIC UINT8
SettingOn (IN CONST CHAR8 *Key)
{
  UINT8 Value;

  return SettingByte (Key, &Value) && Value != 0 ? 1 : 0;
}

QUIRKS_ABL_SETTINGS
QuirksAblSettings (VOID)
{
  QUIRKS_ABL_SETTINGS Settings = {0};
  UINT8               Value;
  // Mapped from the settings' numbering (Auto 0, Internal 1, SD card 2,
  // USB 3) to devinfo's.
  UINT8 BootSource;

  if (SettingByte (SETTINGS_KEY_BOOT_SOURCE, &Value)) {
    BootSource = Value == 1 ? 0 : Value == 2 ? 3 : Value == 3 ? 2 : 1;
  } else {
    BootSource = 1;
  }
  Settings.BootMode    = SettingOn (SETTINGS_KEY_BOOT_MODE);
  Settings.AblMode     = SettingOn (SETTINGS_KEY_ABL_MODE);
  Settings.AltVolumeUp = SettingOn (SETTINGS_KEY_ALT_VOLUME_UP);
  Settings.BootSource  = BootSource;
  if (SettingByte (SETTINGS_KEY_OFFMODE_CHARGING, &Value)) {
    Settings.OffmodeCharging = Value != 0 ? 1 : 0;
  } else {
    Settings.OffmodeCharging = QUIRKS_SETTING_UNSET;
  }
  return Settings;
}

STATIC VOID EFIAPI
PBootPrep (
  IN OPENABL_STORAGE_PROTOCOL *This,
  IN UINT32                    Flags,
  IN OPENABL_STORAGE_LOG       Log OPTIONAL
)
{
  STATIC BOOLEAN Prepared;
  LOG_SINK       Sink = QuirksAblLog (Log);

  (VOID)This;
  if (Flags & OPENABL_PREP_BOOT && !Prepared) {
    Prepared = TRUE;
    SignalGroup (&gEfiEventReadyToBootGuid);
    SignalGroup (&gEfiEndOfDxeEventGroupGuid);
    LogLine (Sink, "boot: ReadyToBoot and EndOfDxe signalled");
  }
  QuirksPrepareStorage (mInstalledChip, Sink);
}

EFI_STATUS
QuirksInstallProtocol (IN UINT32 Chip)
{
  OPENABL_STORAGE_PROTOCOL *Protocol;
  EFI_HANDLE                Handle = NULL;

  mInstalledChip = Chip;
  Protocol       = AllocatePool (sizeof (*Protocol));
  if (!Protocol) {
    return EFI_OUT_OF_RESOURCES;
  }
  Protocol->AndroidParams = PAndroidParams;
  Protocol->BootPrep      = PBootPrep;
  Protocol->Run           = mRunFn;
  Protocol->LogSink       = AblConsoleSink;
  // Never freed: the interface lives as long as the menu.
  return gBS->InstallProtocolInterface (
    &Handle, &mStorageGuid, EFI_NATIVE_INTERFACE, Protocol
  );
}

// Security override

STATIC EFI_STATUS EFIAPI
Accept (
  IN CONST EFI_SECURITY_ARCH_PROTOCOL *This,
  IN UINT32                            AuthenticationStatus,
  IN CONST EFI_DEVICE_PATH_PROTOCOL   *File
)
{
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI
Accept2 (
  IN CONST EFI_SECURITY2_ARCH_PROTOCOL *This,
  IN CONST EFI_DEVICE_PATH_PROTOCOL    *DevicePath OPTIONAL,
  IN VOID                              *FileBuffer OPTIONAL,
  IN UINTN                              FileSize,
  IN BOOLEAN                            BootPolicy
)
{
  return EFI_SUCCESS;
}

STATIC BOOLEAN mDone;

VOID
SecurityAcceptAll (IN LOG_SINK Log)
{
  EFI_SECURITY_ARCH_PROTOCOL  *Security;
  EFI_SECURITY2_ARCH_PROTOCOL *Security2;
  CHAR8                        Line[96] = "security: ";

  if (mDone) {
    return;
  }
  mDone = TRUE;
  // The function pointers are writable data; replace them in place.
  Security = LocateProtocol (&gEfiSecurityArchProtocolGuid);
  if (Security) {
    Security->FileAuthenticationState = Accept;
  } else {
    AsciiStrAppend (Line, sizeof (Line), "no Security protocol; ");
  }
  Security2 = LocateProtocol (&gEfiSecurity2ArchProtocolGuid);
  if (Security2) {
    Security2->FileAuthentication = Accept2;
  } else {
    AsciiStrAppend (Line, sizeof (Line), "no Security2 protocol; ");
  }
  AsciiStrAppend (Line, sizeof (Line), "every image accepted");
  LogLine (Log, Line);
}
