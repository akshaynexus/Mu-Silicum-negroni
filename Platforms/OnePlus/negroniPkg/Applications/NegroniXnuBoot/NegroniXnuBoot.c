/* SPDX-License-Identifier: BSD-2-Clause-Patent */

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>

#define PAYLOAD_PATH       L"\\negroni\\payload.bin"
#define BOOT_LINE_LENGTH   608
#define PAGE_16K           0x4000ULL

#pragma pack(1)
typedef struct {
  UINT64  VmAddr, VmSize, FileOff, FileSize;
} NEGRONI_SEG;

typedef struct {
  CHAR8        Magic[8];
  UINT32       Version;
  UINT32       NSegs;
  UINT64       VirtBase;
  UINT64       EntryVa;
  UINT64       AdtOff, AdtSize;
  UINT64       KernelEnd;
  UINT64       PhysBase;
  UINT64       MemSize;
  CHAR8        CmdLine[BOOT_LINE_LENGTH];
  NEGRONI_SEG  Segs[];
} NEGRONI_PAYLOAD;
#pragma pack()

typedef struct {
  UINT64  BaseAddr, Display, RowBytes, Width, Height, Depth;
} BOOT_VIDEO;

typedef struct {
  UINT16      Revision, Version;
  UINT32      Pad0;
  UINT64      VirtBase, PhysBase, MemSize, TopOfKernelData;
  BOOT_VIDEO  Video;
  UINT32      MachineType;
  UINT32      Pad1;
  UINT64      DeviceTreeP;
  UINT32      DeviceTreeLength;
  CHAR8       CommandLine[BOOT_LINE_LENGTH];
  UINT32      Pad2;
  UINT64      BootFlags, MemSizeActual;
} BOOT_ARGS;

STATIC_ASSERT (OFFSET_OF (NEGRONI_PAYLOAD, Segs) == 0x2a8, "payload header layout");
STATIC_ASSERT (OFFSET_OF (BOOT_ARGS, Video) == 40, "boot_args layout");
STATIC_ASSERT (OFFSET_OF (BOOT_ARGS, CommandLine) == 108, "boot_args layout");
STATIC_ASSERT (sizeof (BOOT_ARGS) == 736, "boot_args layout");

VOID
EFIAPI
NegroniEnterXnu (
  UINT64  BootArgsPa,
  UINT64  EntryPa,
  UINT64  CleanBase,
  UINT64  CleanSize
  );

STATIC
EFI_STATUS
Fail (
  IN CONST CHAR16  *Why,
  IN EFI_STATUS    Status
  )
{
  Print (L"negroni-ios: %s (%r)\n", Why, Status);
  gBS->Stall (10 * 1000 * 1000);
  return Status;
}

STATIC
EFI_STATUS
ReadPayload (
  IN  EFI_HANDLE  ImageHandle,
  OUT VOID        **Buffer,
  OUT UINTN       *Size
  )
{
  EFI_LOADED_IMAGE_PROTOCOL        *Image;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;
  EFI_FILE_PROTOCOL                *Root, *File;
  EFI_FILE_INFO                    *Info;
  UINTN                            InfoSize = SIZE_OF_EFI_FILE_INFO + 256;
  EFI_PHYSICAL_ADDRESS             Pa = 0xFFFFFFFF;
  EFI_STATUS                       Status;

  Status = gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Image);
  if (!EFI_ERROR (Status)) {
    Status = gBS->HandleProtocol (Image->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
  }
  if (!EFI_ERROR (Status)) {
    Status = Fs->OpenVolume (Fs, &Root);
  }
  if (!EFI_ERROR (Status)) {
    Status = Root->Open (Root, &File, PAYLOAD_PATH, EFI_FILE_MODE_READ, 0);
  }
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Info   = AllocatePool (InfoSize);
  Status = Info ? File->GetInfo (File, &gEfiFileInfoGuid, &InfoSize, Info) : EFI_OUT_OF_RESOURCES;
  if (!EFI_ERROR (Status)) {
    *Size  = (UINTN)Info->FileSize;
    Status = gBS->AllocatePages (AllocateMaxAddress, EfiLoaderData, EFI_SIZE_TO_PAGES (*Size), &Pa);
  }
  if (!EFI_ERROR (Status)) {
    *Buffer = (VOID *)(UINTN)Pa;
    Status  = File->Read (File, Size, *Buffer);
  }

  if (Info) {
    FreePool (Info);
  }
  File->Close (File);
  return Status;
}

STATIC
EFI_STATUS
CheckWindow (
  IN UINT64  Base,
  IN UINT64  Size,
  OUT UINTN  *MapKey
  )
{
  EFI_MEMORY_DESCRIPTOR  *Map = NULL, *D;
  UINTN                  MapSize = 0, DescSize;
  UINT32                 DescVer;
  EFI_STATUS             Status;

  do {
    Status = gBS->GetMemoryMap (&MapSize, Map, MapKey, &DescSize, &DescVer);
    if (Status == EFI_BUFFER_TOO_SMALL) {
      if (Map) {
        FreePool (Map);
      }
      MapSize += 8 * DescSize;
      Map      = AllocatePool (MapSize);
      if (Map == NULL) {
        return EFI_OUT_OF_RESOURCES;
      }
      Status = EFI_BUFFER_TOO_SMALL;
    }
  } while (Status == EFI_BUFFER_TOO_SMALL);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  for (D = Map; (UINT8 *)D < (UINT8 *)Map + MapSize; D = NEXT_MEMORY_DESCRIPTOR (D, DescSize)) {
    UINT64  End = D->PhysicalStart + EFI_PAGES_TO_SIZE (D->NumberOfPages);

    if ((End <= Base) || (D->PhysicalStart >= Base + Size)) {
      continue;
    }
    switch (D->Type) {
      case EfiConventionalMemory:
      case EfiBootServicesCode:
      case EfiBootServicesData:
      case EfiLoaderCode:
      case EfiLoaderData:
        continue;
      default:
        Print (L"negroni-ios: kernel window overlaps type %u at 0x%lx-0x%lx\n", D->Type, D->PhysicalStart, End);
        return EFI_ACCESS_DENIED;
    }
  }
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
NegroniXnuBootEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  NEGRONI_PAYLOAD               *P;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;
  BOOT_VIDEO                    Video = { 0 };
  UINTN                         Size, MapKey;
  EFI_STATUS                    Status;

  Print (L"negroni-ios: loading %s\n", PAYLOAD_PATH);
  Status = ReadPayload (ImageHandle, (VOID **)&P, &Size);
  if (EFI_ERROR (Status)) {
    return Fail (L"cannot read the payload", Status);
  }
  if ((CompareMem (P->Magic, "NEGRONIX", 8) != 0) || (P->Version != 1) || (P->AdtOff & 7) ||
      (sizeof (*P) + P->NSegs * sizeof (NEGRONI_SEG) > Size) || (P->AdtOff + P->AdtSize > Size))
  {
    return Fail (L"not a negroni-ios payload", EFI_LOAD_ERROR);
  }
  for (UINT32 i = 0; i < P->NSegs; i++) {
    if (P->Segs[i].FileOff + P->Segs[i].FileSize > Size) {
      return Fail (L"segment outside the payload", EFI_LOAD_ERROR);
    }
  }

  if (!EFI_ERROR (gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop))) {
    Video.BaseAddr = Gop->Mode->FrameBufferBase;
    Video.Display  = 1;
    Video.RowBytes = (UINT64)Gop->Mode->Info->PixelsPerScanLine * 4;
    Video.Width    = Gop->Mode->Info->HorizontalResolution;
    Video.Height   = Gop->Mode->Info->VerticalResolution;
    Video.Depth    = 32;
  }

  UINT64  PaOfVa  = P->PhysBase - P->VirtBase;
  UINT64  AdtPa   = P->KernelEnd + PaOfVa;
  UINT64  BaPa    = (AdtPa + P->AdtSize + PAGE_16K - 1) & ~(PAGE_16K - 1);
  UINT64  Top     = BaPa + PAGE_16K;
  UINT64  EntryPa = P->EntryVa + PaOfVa;

  Print (L"negroni-ios: kernel 0x%lx-0x%lx entry 0x%lx, RAM 0x%lx+0x%lx, fb 0x%lx %lux%lu\n",
         P->PhysBase, Top, EntryPa, P->PhysBase, P->MemSize, Video.BaseAddr, Video.Width, Video.Height);
  if (((UINT64)(UINTN)P < P->PhysBase + P->MemSize) && ((UINT64)(UINTN)P + Size > P->PhysBase)) {
    return Fail (L"payload buffer inside the kernel window", EFI_BAD_BUFFER_SIZE);
  }

  for (UINTN Try = 0; ; Try++) {
    Status = CheckWindow (P->PhysBase, P->MemSize, &MapKey);
    if (EFI_ERROR (Status)) {
      return Fail (L"kernel window is not free RAM", Status);
    }
    Status = gBS->ExitBootServices (ImageHandle, MapKey);
    if (!EFI_ERROR (Status) || (Try == 1)) {
      break;
    }
  }
  if (EFI_ERROR (Status)) {
    return Fail (L"ExitBootServices", Status);
  }

  for (UINT32 i = 0; i < P->NSegs; i++) {
    NEGRONI_SEG  *S  = &P->Segs[i];
    UINT8        *To = (UINT8 *)(UINTN)(S->VmAddr + PaOfVa);

    CopyMem (To, (UINT8 *)P + S->FileOff, S->FileSize);
    if (S->VmSize > S->FileSize) {
      ZeroMem (To + S->FileSize, S->VmSize - S->FileSize);
    }
  }
  CopyMem ((VOID *)(UINTN)AdtPa, (UINT8 *)P + P->AdtOff, P->AdtSize);

  BOOT_ARGS  *Ba = (BOOT_ARGS *)(UINTN)BaPa;

  ZeroMem (Ba, PAGE_16K);
  Ba->Revision         = 2;
  Ba->Version          = 2;
  Ba->VirtBase         = P->VirtBase;
  Ba->PhysBase         = P->PhysBase;
  Ba->MemSize          = P->MemSize;
  Ba->TopOfKernelData  = Top;
  Ba->Video            = Video;
  Ba->DeviceTreeP      = AdtPa - PaOfVa;
  Ba->DeviceTreeLength = (UINT32)P->AdtSize;
  CopyMem (Ba->CommandLine, P->CmdLine, BOOT_LINE_LENGTH);
  Ba->MemSizeActual = P->MemSize;

  NegroniEnterXnu (BaPa, EntryPa, P->PhysBase, Top - P->PhysBase);
  return EFI_LOAD_ERROR;
}
