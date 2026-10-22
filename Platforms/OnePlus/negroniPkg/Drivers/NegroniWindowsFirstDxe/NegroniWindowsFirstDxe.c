#include <Uefi.h>

#include <Guid/FileInfo.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/SimpleFileSystem.h>

STATIC CONST CHAR16  mBootMgr[] = L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi";
STATIC VOID          *mRegistration;
STATIC BOOLEAN       mDone;

STATIC
BOOLEAN
HasBootMgr (
  IN EFI_HANDLE  Handle
  )
{
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;
  EFI_FILE_PROTOCOL                *Root;
  EFI_FILE_PROTOCOL                *File;

  if (EFI_ERROR (gBS->HandleProtocol (Handle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs)) ||
      EFI_ERROR (Fs->OpenVolume (Fs, &Root)))
  {
    return FALSE;
  }

  if (EFI_ERROR (Root->Open (Root, &File, (CHAR16 *)mBootMgr, EFI_FILE_MODE_READ, 0))) {
    Root->Close (Root);
    return FALSE;
  }

  File->Close (File);
  Root->Close (Root);
  return TRUE;
}

STATIC
VOID
MakeFirst (
  IN EFI_HANDLE  Handle
  )
{
  EFI_STATUS                    Status;
  EFI_DEVICE_PATH_PROTOCOL      *Path;
  EFI_BOOT_MANAGER_LOAD_OPTION  Option;
  EFI_BOOT_MANAGER_LOAD_OPTION  *Options;
  UINTN                         Count;
  INTN                          Index;

  Path = FileDevicePath (Handle, mBootMgr);
  if (Path == NULL) {
    return;
  }

  Status = EfiBootManagerInitializeLoadOption (&Option, LoadOptionNumberUnassigned, LoadOptionTypeBoot, LOAD_OPTION_ACTIVE, L"Windows Boot Manager", Path, NULL, 0);
  FreePool (Path);
  if (EFI_ERROR (Status)) {
    return;
  }

  Options = EfiBootManagerGetLoadOptions (&Count, LoadOptionTypeBoot);
  Index   = EfiBootManagerFindLoadOption (&Option, Options, Count);
  if (Index >= 0) {
    EfiBootManagerDeleteLoadOptionVariable (Options[Index].OptionNumber, LoadOptionTypeBoot);
  }

  EfiBootManagerFreeLoadOptions (Options, Count);
  Status = EfiBootManagerAddLoadOptionVariable (&Option, 0);
  DEBUG ((DEBUG_INFO, "NegroniWindowsFirst: Boot%04x first in BootOrder: %r\n", Option.OptionNumber, Status));
  EfiBootManagerFreeLoadOption (&Option);
  mDone = !EFI_ERROR (Status);
}

STATIC
VOID
EFIAPI
OnFileSystem (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_HANDLE  Handle;
  UINTN       Size;

  while (!mDone) {
    Size = sizeof (Handle);
    if (EFI_ERROR (gBS->LocateHandle (ByRegisterNotify, NULL, mRegistration, &Size, &Handle))) {
      return;
    }

    if (HasBootMgr (Handle)) {
      MakeFirst (Handle);
    }
  }

  gBS->CloseEvent (Event);
}

EFI_STATUS
EFIAPI
NegroniWindowsFirstEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EfiCreateProtocolNotifyEvent (&gEfiSimpleFileSystemProtocolGuid, TPL_CALLBACK, OnFileSystem, NULL, &mRegistration);
  return EFI_SUCCESS;
}
