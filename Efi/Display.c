/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Display - draws frames through the Graphics Output Protocol.
*/

#include "Efi/Display.h"

#include "Base/Memory.h"
#include "Efi/Handover.h"

// GET_PROTOCOL leaves the console driver bound to the GOP. We only call Blt
// and read the mode.
STATIC EFI_STATUS
OpenGop (
  IN EFI_HANDLE                      Handle,
  OUT EFI_GRAPHICS_OUTPUT_PROTOCOL **Gop
)
{
  VOID      *Interface = NULL;
  EFI_STATUS Status    = gBS->OpenProtocol (
    Handle, &gEfiGraphicsOutputProtocolGuid, &Interface, gImageHandle, NULL,
    EFI_OPEN_PROTOCOL_GET_PROTOCOL
  );

  *Gop = Interface;
  return Status;
}

STATIC DISPLAY
Opened (IN EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop)
{
  return (DISPLAY){Gop, Gop->Mode->Info->HorizontalResolution,
                   Gop->Mode->Info->VerticalResolution};
}

EFI_STATUS
DisplayOpen (OUT DISPLAY *Out)
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
  EFI_HANDLE                   *Handles = NULL;
  EFI_HANDLE                    Handle;
  UINTN                         Count = 0;
  EFI_STATUS                    Status;

  Status = gBS->LocateHandleBuffer (
    ByProtocol, &gEfiGraphicsOutputProtocolGuid, NULL, &Count, &Handles
  );
  if (Status != EFI_SUCCESS) {
    return Status;
  }
  Handle = Count ? Handles[0] : NULL;
  if (Handles) {
    gBS->FreePool (Handles);
  }
  if (!Handle) {
    return EFI_NOT_FOUND;
  }
  Status = OpenGop (Handle, &Gop);
  if (Status != EFI_SUCCESS) {
    return Status;
  }
  *Out = Opened (Gop);
  return EFI_SUCCESS;
}

EFI_STATUS
DisplayOpenPanel (OUT DISPLAY *Out)
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *First        = NULL;
  EFI_HANDLE                   *Handles      = NULL;
  UINT64                        RegionBase   = 0;
  UINT64                        RegionLength = 0;
  BOOLEAN                       Region;
  UINTN                         Count = 0;
  EFI_STATUS                    Status;

  Status = gBS->LocateHandleBuffer (
    ByProtocol, &gEfiGraphicsOutputProtocolGuid, NULL, &Count, &Handles
  );
  if (Status != EFI_SUCCESS) {
    return Status;
  }
  Region = HandoverDisplayRegion (&RegionBase, &RegionLength);
  for (UINTN Index = 0; Index < Count; Index++) {
    UINT64 Base;

    if (OpenGop (Handles[Index], &Gop) != EFI_SUCCESS) {
      continue;
    }
    Base = Gop->Mode->FrameBufferBase;
    if (Gop->Mode->Info->PixelFormat != PixelBltOnly && Region &&
        RegionBase <= Base && Base < RegionBase + RegionLength) {
      gBS->FreePool (Handles);
      *Out = Opened (Gop);
      return EFI_SUCCESS;
    }
    if (!First) {
      First = Gop;
    }
  }
  if (Handles) {
    gBS->FreePool (Handles);
  }
  if (!First) {
    return EFI_NOT_FOUND;
  }
  *Out = Opened (First);
  return EFI_SUCCESS;
}

#if defined(__aarch64__)
STATIC inline UINT32
DisplayNowUs (VOID)
{
  UINT64 Ticks;
  __asm__ volatile ("isb\n\tmrs %0, cntvct_el0" : "=r"(Ticks));
  return (UINT32)(Ticks * 10ULL / 192ULL);
}
#endif

VOID
DisplayBlit (
  IN DISPLAY      *Display,
  IN CONST UINT32 *Pixels,
  IN UINT32        Stride,
  IN UINT32        X,
  IN UINT32        Y,
  IN UINT32        W,
  IN UINT32        H
)
{
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info = Display->Gop->Mode->Info;

  if ((UINT64)X + W > Info->HorizontalResolution ||
      (UINT64)Y + H > Info->VerticalResolution || W == 0 || H == 0) {
    return;
  }
#if defined(__aarch64__)
  if (Info->HorizontalResolution == 1080 && Info->VerticalResolution == 1920 &&
      (*(volatile CONST UINT32 *)(UINTN)0x0AE94004ULL & 0x1U) != 0U &&
      (*(volatile CONST UINT32 *)(UINTN)0x0AE05014ULL & 0xFE000000U) ==
        0x9C000000U) {
    STATIC UINT32 *mShadowFb     = NULL;
    STATIC BOOLEAN mRingInit     = FALSE;
    STATIC BOOLEAN mTearInit     = FALSE;
    STATIC UINT64  mDirtyMask[2] = {0};
    CONST UINT64   AllStripsMask = (1ULL << 60) - 1ULL;
    UINT32         WaitStartUs;
    UINTN          Pitch  = (UINTN)Info->PixelsPerScanLine * 4U;
    UINTN          FbSize = (UINTN)Info->VerticalResolution * Pitch;
    UINTN          ScanoutIdx;
    UINTN          NextIdx;
    UINT32         FirstStrip;
    UINT32         LastStrip;
    UINT64         BlitMask;
    UINT64         CopyMask;
    UINT64         CleanMask;
    UINT64         DstBase;

    if (!mShadowFb) {
      mShadowFb = AllocatePool (FbSize);
      if (mShadowFb) {
        ZeroMem (mShadowFb, FbSize);
      }
    }

    if (!mRingInit) {
      mRingInit     = TRUE;
      mDirtyMask[0] = AllStripsMask;
      mDirtyMask[1] = AllStripsMask;
    }

    // One-time hardware TearCheck fix for the <DSITEUsingDedicatedTEPin>True
    // tag omitted in Retroid's Panel_vtdr6130_visionox_cog_1080p_cmd.xml:
    //   0x0AE6BA88 (SYNC_CONFIG_VSYNC) |= BIT(20) (hw_vsync_mode = 1)
    // With DisplayDxe's VSYNC_INIT_VAL = 0 and START_POS = 21 (0x15), each
    // physical 60 Hz TE rising edge on GPIO 66 resets rd_ptr_line_count to 0
    // (instead of counting to 0xFFFF = 566 ms) and kicks off the DSI frame
    // transfer 21 lines (0.18 ms) into the panel's 8.50 ms VBLANK.
    if (!mTearInit) {
      UINT32 VsyncCfg = *(volatile CONST UINT32 *)(UINTN)0x0AE6BA88ULL;
      mTearInit       = TRUE;
      *(volatile UINT32 *)(UINTN)0x0AE6BA88ULL = VsyncCfg | 0x00100000U;
      *(volatile UINT32 *)(UINTN)0x0AE6B0A8ULL = 1U;
      __asm__ volatile ("dsb sy" : : : "memory");
    }

    // Wait if a previous page flip is still pending in CTL_0_FLUSH
    WaitStartUs = DisplayNowUs ();
    while ((*(volatile CONST UINT32 *)(UINTN)0x0AE02018ULL & 0x1U) != 0U) {
      if (DisplayNowUs () - WaitStartUs > 25000U) {
        break;
      }
    }

    ScanoutIdx =
      ((*(volatile CONST UINT32 *)(UINTN)0x0AE05014ULL) >= 0x9C800000U) ? 1U
                                                                        : 0U;
    NextIdx    = ScanoutIdx ^ 1U;
    FirstStrip = Y / 32U;
    LastStrip  = (Y + H - 1U) / 32U;
    BlitMask   = ((2ULL << LastStrip) - 1ULL) & ~((1ULL << FirstStrip) - 1ULL);
    CopyMask   = mDirtyMask[NextIdx];
    if (X == 0 && Y == 0 && W == Info->HorizontalResolution &&
        H == Info->VerticalResolution) {
      CopyMask = 0ULL;
    }
    CleanMask = CopyMask | BlitMask;
    DstBase   = 0x9C000000ULL + (UINT64)NextIdx * 0x00800000ULL;

    if (mShadowFb) {
      UINT8 *ShadowBytes = (UINT8 *)mShadowFb;
      if (X == 0 && W == Info->HorizontalResolution &&
          Stride == Info->PixelsPerScanLine) {
        gBS->CopyMem (
          ShadowBytes + (UINTN)Y * Pitch, (VOID *)Pixels, (UINTN)H * Pitch
        );
      } else {
        for (UINT32 Row = 0; Row < H; Row++) {
          gBS->CopyMem (
            ShadowBytes + (UINTN)(Y + Row) * Pitch + (UINTN)X * 4U,
            (VOID *)(Pixels + (UINTN)Row * Stride), (UINTN)W * 4U
          );
        }
      }

      for (UINT32 Strip = 0; Strip < 60U; Strip++) {
        if ((CleanMask & (1ULL << Strip)) != 0) {
          UINTN  Offset   = (UINTN)Strip * 32U * Pitch;
          UINT32 StripEnd = Strip + 1U;
          while (StripEnd < 60U && (CleanMask & (1ULL << StripEnd)) != 0) {
            StripEnd++;
          }
          gBS->CopyMem (
            (VOID *)(UINTN)(DstBase + Offset), ShadowBytes + Offset,
            (UINTN)(StripEnd - Strip) * 32U * Pitch
          );
          Strip = StripEnd - 1U;
        }
      }
    }

    for (UINT32 Strip = 0; Strip < 60U; Strip++) {
      if ((CleanMask & (1ULL << Strip)) != 0) {
        UINTN  Start    = (UINTN)DstBase + (UINTN)Strip * 32U * Pitch;
        UINT32 StripEnd = Strip + 1U;
        while (StripEnd < 60U && (CleanMask & (1ULL << StripEnd)) != 0) {
          StripEnd++;
        }
        UINTN End = (UINTN)DstBase + (UINTN)StripEnd * 32U * Pitch;
        for (UINTN Addr = Start; Addr < End; Addr += 64U) {
          __asm__ volatile ("dc cvac, %0" : : "r"(Addr) : "memory");
        }
        Strip = StripEnd - 1U;
      }
    }
    __asm__ volatile ("dsb sy" : : : "memory");

    mDirtyMask[ScanoutIdx] |= BlitMask;
    mDirtyMask[NextIdx]     = 0ULL;

    // Commit SSPP_VIG0_SRC0_ADDR and arm CTL_0_FLUSH (BIT(0) VIG0) for the
    // next TE-gated hardware autorefresh kickoff.
    *(volatile UINT32 *)(UINTN)0x0AE05014ULL = (UINT32)DstBase;
    __asm__ volatile ("dsb sy" : : : "memory");
    *(volatile UINT32 *)(UINTN)0x0AE02018ULL = 0x00000001U;
    __asm__ volatile ("dsb sy" : : : "memory");

    // Wait up to 25 ms for CTL_0_FLUSH to be consumed by the TE-gated kickoff.
    WaitStartUs = DisplayNowUs ();
    while ((*(volatile CONST UINT32 *)(UINTN)0x0AE02018ULL & 0x1U) != 0U) {
      if (DisplayNowUs () - WaitStartUs > 25000U) {
        break;
      }
    }
    return;
  }
#endif
  // A BltPixel is {blue, green, red, reserved}, which is a little-endian
  // 0x00RRGGBB.
  Display->Gop->Blt (
    Display->Gop, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)Pixels, EfiBltBufferToVideo,
    0, 0, X, Y, W, H, (UINTN)Stride * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL)
  );
}
