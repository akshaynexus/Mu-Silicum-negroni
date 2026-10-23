/** @file
  NegroniPcieDxe log protocol: the PCIe0 (Wi-Fi) bring-up log as text, and a rerun of the bring-up.
  Consumed by the NegroniWifiDiag application (boot entry "Wi-Fi diagnostics").

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef NEGRONI_PCIE_LOG_H_
#define NEGRONI_PCIE_LOG_H_

#define NEGRONI_PCIE_LOG_PROTOCOL_GUID \
  { 0x9a4d27c1, 0x5b63, 0x4e18, { 0xb2, 0x7f, 0x0d, 0x61, 0xc8, 0x3e, 0x95, 0x42 } }

// FILE_GUID of NegroniWifiDiag.inf
#define NEGRONI_WIFI_DIAG_FILE_GUID \
  { 0x4f2b8e63, 0x1c07, 0x4d95, { 0x8a, 0x3e, 0x72, 0xb5, 0x19, 0xd0, 0x6c, 0x2f } }

#define NEGRONI_PCIE_LOG_SIZE  8192

typedef struct _NEGRONI_PCIE_LOG_PROTOCOL NEGRONI_PCIE_LOG_PROTOCOL;

typedef
EFI_STATUS
(EFIAPI *NEGRONI_PCIE_RERUN)(
  VOID
  );

struct _NEGRONI_PCIE_LOG_PROTOCOL {
  CONST CHAR8         *Log;    // NUL-terminated, newline separated
  NEGRONI_PCIE_RERUN  Rerun;   // clears the log and runs the bring-up again
};

#endif
