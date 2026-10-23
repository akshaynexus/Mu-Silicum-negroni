/** @file
  "Wi-Fi diagnostics" boot entry: shows the NegroniPcieDxe bring-up log (PCIe0 link to the QCA6490).
  Volume Up runs the bring-up again; any other key returns to the firmware menu.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include "../../Drivers/NegroniPcieDxe/NegroniPcieLog.h"

STATIC EFI_GUID  mLogProtocolGuid = NEGRONI_PCIE_LOG_PROTOCOL_GUID;

STATIC
VOID
Show (
  IN CONST NEGRONI_PCIE_LOG_PROTOCOL  *Pcie
  )
{
  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"negroni Wi-Fi diagnostics (PCIe0, QCA6490 17cb:1103)\n\n");
  Print (L"%a\n", Pcie->Log);
  Print (L"Volume Up: bring the link up again   other key: back\n");
}

EFI_STATUS
EFIAPI
NegroniWifiDiagEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  NEGRONI_PCIE_LOG_PROTOCOL  *Pcie;
  EFI_INPUT_KEY              Key;
  UINTN                      Index;
  EFI_STATUS                 Status;

  Status = gBS->LocateProtocol (&mLogProtocolGuid, NULL, (VOID **)&Pcie);
  if (EFI_ERROR (Status)) {
    Print (L"NegroniPcieDxe is not loaded (%r)\n", Status);
    gBS->Stall (5 * 1000 * 1000);
    return Status;
  }

  for ( ; ;) {
    Show (Pcie);
    gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Index);
    if (EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))) {
      continue;
    }

    if (Key.ScanCode != SCAN_UP) {
      return EFI_SUCCESS;
    }

    Print (L"\nbringing the link up...\n");
    Pcie->Rerun ();
  }
}
