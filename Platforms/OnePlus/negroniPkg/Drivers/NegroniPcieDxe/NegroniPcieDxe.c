#include <Uefi.h>

#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/EFIClock.h>
#include <Protocol/LoadedImage.h>

#include "NegroniPcieLog.h"

#include "NegroniPciePhy.h"

// PCIe0 (QCA6490 Wi-Fi), addresses from Linux sm8450.dtsi
#define GCC_BASE         0x00100000UL
#define GCC_PCIE_0_BCR   0x7b000
#define GCC_PCIE_0_PHY_BCR  0x7c01c
#define GCC_PCIE_0_NOCSR_COM_PHY_BCR  0x7c020
#define GCC_PCIE_0_PIPE_MUX  0x7b060
#define TLMM_BASE        0x0F100000UL
#define PARF             0x01C00000UL
#define PHY              0x01C06000UL
#define DBI              0x60000000UL
#define ATU              0x60001000UL
#define CFG_BUS1         0x60100000UL
#define MEM_BASE         0x60300000UL
#define MEM_LIMIT        0x63FFFFFFUL

#define WLAN_EN_GPIO     80
#define PERST_GPIO       94
#define XO_CLK_GPIO      204     /* DT wcn6855-pmu xo-clk-gpios */

#define PARF_SYS_CTRL                   0x00
#define PARF_PM_CTRL                    0x20
#define PARF_PHY_CTRL                   0x40
#define PARF_MHI_CLOCK_RESET_CTRL       0x174
#define PARF_AXI_MSTR_WR_ADDR_HALT_V2   0x1a8
#define PARF_LTSSM                      0x1b0
#define PARF_DBI_BASE_ADDR_V2           0x350
#define PARF_DBI_BASE_ADDR_V2_HI        0x354
#define PARF_ATU_BASE_ADDR              0x634
#define PARF_ATU_BASE_ADDR_HI           0x638
#define PARF_SLV_ADDR_SPACE_SIZE_V2     0x358
#define PARF_SLV_ADDR_SPACE_SIZE_V2_HI  0x35c
#define PARF_DEVICE_TYPE                0x1000
#define ELBI_SYS_STTS                   0x08

#define DBI_MISC_CONTROL_1              0x8bc
#define DBI_LINK_CAP_EXP                0x7c

STATIC CONST CHAR8  *mClocks[] = {
  "gcc_pcie_0_aux_clk",
  "gcc_pcie_0_cfg_ahb_clk",
  "gcc_pcie_0_mstr_axi_clk",
  "gcc_pcie_0_slv_axi_clk",
  "gcc_pcie_0_slv_q2a_axi_clk",
  "gcc_ddrss_pcie_sf_tbu_clk",
  "gcc_aggre_noc_pcie_0_axi_clk",
  "gcc_aggre_noc_pcie_1_axi_clk",
  "gcc_pcie_0_clkref_en",
  "gcc_pcie_0_phy_rchng_clk",
};

STATIC EFI_CLOCK_PROTOCOL  *mClock;

STATIC CHAR8   mLog[NEGRONI_PCIE_LOG_SIZE];
STATIC UINTN   mLogLen;
STATIC EFI_GUID  mLogGuid  = NEGRONI_PCIE_LOG_PROTOCOL_GUID;
STATIC EFI_GUID  mDiagGuid = NEGRONI_WIFI_DIAG_FILE_GUID;

/* Into the debug output and the log the "Wi-Fi diagnostics" entry shows. */
STATIC
VOID
EFIAPI
Log (
  IN CONST CHAR8  *Format,
  ...
  )
{
  VA_LIST  Args;
  UINTN    Len;

  VA_START (Args, Format);
  Len = AsciiVSPrint (mLog + mLogLen, sizeof (mLog) - mLogLen, Format, Args);
  VA_END (Args);
  DEBUG ((DEBUG_ERROR, "NPCIE: %a", mLog + mLogLen));
  mLogLen += Len;
}

// the PCIe register blocks are not in the platform memory map
STATIC
EFI_STATUS
MapMmio (
  IN UINT64  Base,
  IN UINT64  Size
  )
{
  EFI_STATUS  Status;

  Status = gDS->AddMemorySpace (EfiGcdMemoryTypeMemoryMappedIo, Base, Size, EFI_MEMORY_UC | EFI_MEMORY_RUNTIME);
  if (EFI_ERROR (Status) && (Status != EFI_ACCESS_DENIED)) {
    return Status;
  }

  return gDS->SetMemorySpaceAttributes (Base, Size, EFI_MEMORY_UC);
}

STATIC
VOID
Gpio (
  IN UINTN    Pin,
  IN BOOLEAN  High
  )
{
  // function 0 (gpio), 16 mA, no pull, output enable
  MmioWrite32 (TLMM_BASE + Pin * 0x1000 + 4, High ? BIT1 : 0);
  MmioWrite32 (TLMM_BASE + Pin * 0x1000, (7 << 6) | BIT9);
}

STATIC
VOID
Bcr (
  IN UINTN  Reg
  )
{
  MmioWrite32 (GCC_BASE + Reg, 1);
  gBS->Stall (1000);
  MmioWrite32 (GCC_BASE + Reg, 0);
  gBS->Stall (1000);
}

STATIC
EFI_STATUS
ClockOn (
  IN CONST CHAR8  *Name
  )
{
  UINTN       Id;
  EFI_STATUS  Status;

  Status = mClock->GetClockID (mClock, Name, &Id);
  if (!EFI_ERROR (Status)) {
    Status = mClock->EnableClock (mClock, Id);
  }

  Log ("%a %r\n", Name, Status);
  return Status;
}

STATIC
VOID
Atu (
  IN UINTN   Index,
  IN UINT32  Type,
  IN UINT64  Base,
  IN UINT64  Limit,
  IN UINT64  Target
  )
{
  UINTN  R = ATU + (Index << 9);

  MmioWrite32 (R + 0x08, (UINT32)Base);
  MmioWrite32 (R + 0x0c, (UINT32)(Base >> 32));
  MmioWrite32 (R + 0x10, (UINT32)Limit);
  MmioWrite32 (R + 0x20, (UINT32)(Limit >> 32));
  MmioWrite32 (R + 0x14, (UINT32)Target);
  MmioWrite32 (R + 0x18, (UINT32)(Target >> 32));
  MmioWrite32 (R + 0x00, Type);
  MmioWrite32 (R + 0x04, BIT31);
}

STATIC
EFI_STATUS
LinkUp (
  VOID
  )
{
  UINTN       Index;
  UINTN       Id;
  UINT32      Val;
  EFI_STATUS  Status;

  Status = gBS->LocateProtocol (&gEfiClockProtocolGuid, NULL, (VOID **)&mClock);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = MapMmio (PARF, 0x10000);
  if (!EFI_ERROR (Status)) {
    Status = MapMmio (DBI, MEM_LIMIT + 1 - DBI);
  }

  Log ("map %r\n", Status);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // QCA6490 on (rails are shared with UFS and stay on in UEFI), endpoint in reset
  // Linux pwrseq-qcom-wcn (wcn6855): XO clock pin high, WLAN_EN high, then the XO pin low again and 50 ms
  Gpio (PERST_GPIO, FALSE);
  Gpio (WLAN_EN_GPIO, FALSE);
  Gpio (XO_CLK_GPIO, FALSE);
  gBS->Stall (10000);
  Gpio (XO_CLK_GPIO, TRUE);
  gBS->Stall (200);
  Gpio (WLAN_EN_GPIO, TRUE);
  gBS->Stall (5000);
  gBS->Stall (5000);
  Gpio (XO_CLK_GPIO, FALSE);
  gBS->Stall (50000);
  Log ("WLAN_EN %u, SW_CTRL in %u\n", (MmioRead32 (TLMM_BASE + WLAN_EN_GPIO * 0x1000 + 4) >> 1) & 1,
    MmioRead32 (TLMM_BASE + 82 * 0x1000 + 4) & 1);

  Status = mClock->GetClockPowerDomainID (mClock, "gcc_pcie_0_gdsc", &Id);
  if (!EFI_ERROR (Status)) {
    Status = mClock->EnableClockPowerDomain (mClock, Id);
  }

  Log ("gdsc %r\n", Status);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // PHY reference/aux clocks; the pipe clock comes from the PHY, so XO feeds the mux until it runs
  MmioWrite32 (GCC_BASE + GCC_PCIE_0_PIPE_MUX, 2);
  for (Index = 0; Index < ARRAY_SIZE (mClocks); Index++) {
    if (EFI_ERROR (ClockOn (mClocks[Index]))) {
      return EFI_DEVICE_ERROR;
    }
  }

  // controller (qcom_pcie_init_2_7_0)
  Bcr (GCC_PCIE_0_BCR);
  MmioWrite32 (PARF + PARF_DEVICE_TYPE, 4);
  MmioAnd32 (PARF + PARF_PHY_CTRL, ~(UINT32)BIT0);
  MmioWrite32 (PARF + PARF_DBI_BASE_ADDR_V2, (UINT32)DBI);
  MmioWrite32 (PARF + PARF_DBI_BASE_ADDR_V2_HI, 0);
  MmioWrite32 (PARF + PARF_ATU_BASE_ADDR, (UINT32)ATU);
  MmioWrite32 (PARF + PARF_ATU_BASE_ADDR_HI, 0);
  // the slave window over 0x60000000 (DBI, iATU, bus config, memory): Linux qcom_pcie_configure_dbi_atu_base
  MmioWrite32 (PARF + PARF_SLV_ADDR_SPACE_SIZE_V2, 0);
  MmioWrite32 (PARF + PARF_SLV_ADDR_SPACE_SIZE_V2_HI, 0x80000000);
  MmioAnd32 (PARF + PARF_SYS_CTRL, ~(UINT32)BIT29);
  MmioOr32 (PARF + PARF_MHI_CLOCK_RESET_CTRL, BIT4);
  MmioAnd32 (PARF + PARF_PM_CTRL, ~(UINT32)BIT5);
  MmioOr32 (PARF + PARF_AXI_MSTR_WR_ADDR_HALT_V2, BIT31);

  // PHY (qmp_pcie_init + qmp_pcie_power_on, RC mode)
  MmioWrite32 (GCC_BASE + GCC_PCIE_0_PHY_BCR, 1);
  MmioWrite32 (GCC_BASE + GCC_PCIE_0_NOCSR_COM_PHY_BCR, 1);
  gBS->Stall (300);
  MmioWrite32 (GCC_BASE + GCC_PCIE_0_PHY_BCR, 0);
  MmioOr32 (PHY + PHY_PCS_POWER_DOWN_CONTROL, BIT0 | BIT1);
  for (Index = 0; Index < ARRAY_SIZE (mPhyInit); Index++) {
    MmioWrite32 (PHY + mPhyInit[Index][0], mPhyInit[Index][1]);
  }

  MmioWrite32 (GCC_BASE + GCC_PCIE_0_NOCSR_COM_PHY_BCR, 0);
  MmioAnd32 (PHY + PHY_PCS_SW_RESET, ~(UINT32)BIT0);
  MmioOr32 (PHY + PHY_PCS_START_CONTROL, BIT0 | BIT1);
  gBS->Stall (1200);
  MmioWrite32 (GCC_BASE + GCC_PCIE_0_PIPE_MUX, 0);
  if (EFI_ERROR (ClockOn ("gcc_pcie_0_pipe_clk"))) {
    return EFI_DEVICE_ERROR;
  }
  for (Index = 0; Index < 100 && (MmioRead32 (PHY + PHY_PCS_PCS_STATUS1) & BIT6); Index++) {
    gBS->Stall (200);
  }

  Val = MmioRead32 (PHY + PHY_PCS_PCS_STATUS1);
  Log ("phy status %x\n", Val);
  if (Val & BIT6) {
    return EFI_DEVICE_ERROR;
  }

  // root port as a PCI-to-PCI bridge, bus 0 -> 1; no hot plug (qcom_pcie_post_init_2_7_0, qcom_fixup_class)
  MmioOr32 (DBI + DBI_MISC_CONTROL_1, BIT0);
  MmioWrite32 (DBI + 0x08, (MmioRead32 (DBI + 0x08) & 0xff) | (0x0604 << 16));
  MmioAnd32 (DBI + 0x70 + 0x14, ~(UINT32)BIT6);
  MmioAnd32 (DBI + DBI_MISC_CONTROL_1, ~(UINT32)BIT0);
  MmioWrite32 (DBI + 0x10, 0);
  MmioWrite32 (DBI + 0x14, 0);
  MmioWrite32 (DBI + 0x18, 0x00010100);
  MmioOr32 (DBI + 0x04, BIT1 | BIT2);

  // bus 1 config space right after the root port (ECAM layout), memory window identity mapped
  Atu (0, 4, CFG_BUS1, CFG_BUS1 + 0xfffff, (UINT64)1 << 24);
  Atu (1, 0, MEM_BASE, MEM_LIMIT, MEM_BASE);

  // endpoint out of reset, link training
  gBS->Stall (100000);
  Gpio (PERST_GPIO, TRUE);
  gBS->Stall (1000);
  MmioOr32 (PARF + PARF_LTSSM, BIT8);
  for (Index = 0; Index < 100; Index++) {
    Val = MmioRead32 (DBI + 0x80);
    if (Val & (BIT29 << 0)) {
      break;
    }

    gBS->Stall (10000);
  }

  Log ("link status %x ltssm %x (PARF_LTSSM %x)\n", Val >> 16, MmioRead32 (PARF + 0x1b4), MmioRead32 (PARF + PARF_LTSSM));
  if ((Val & BIT29) == 0) {
    return EFI_TIMEOUT;
  }

  // the endpoint may answer config reads only some time after the link is up
  for (Index = 0; Index < 100; Index++) {
    Val = MmioRead32 (CFG_BUS1);
    if (Val != 0xffffffff) {
      break;
    }

    gBS->Stall (1000);
  }

  Log ("bus 1 device %04x:%04x after %u ms; root port bus regs %08x, cmd %08x\n", Val & 0xffff, Val >> 16,
    (UINT32)Index, MmioRead32 (DBI + 0x18), MmioRead32 (DBI + 0x04));
  Log ("iATU 0: ctrl1 %08x ctrl2 %08x base %08x limit %08x target %08x\n", MmioRead32 (ATU + 0x00),
    MmioRead32 (ATU + 0x04), MmioRead32 (ATU + 0x08), MmioRead32 (ATU + 0x10), MmioRead32 (ATU + 0x14));
  if (Val != 0xffffffff) {
    Log ("bus 1 class %08x, BAR0 %08x\n", MmioRead32 (CFG_BUS1 + 0x08), MmioRead32 (CFG_BUS1 + 0x10));
  }
  return (Val == 0x110317cb) ? EFI_SUCCESS : EFI_NOT_FOUND;
}

STATIC
EFI_STATUS
EFIAPI
Rerun (
  VOID
  )
{
  EFI_STATUS  Status;

  mLogLen = 0;
  mLog[0] = 0;
  Status  = LinkUp ();
  Log ("Wi-Fi link %r\n", Status);
  return Status;
}

STATIC NEGRONI_PCIE_LOG_PROTOCOL  mLogProtocol = { mLog, Rerun };

/* "Wi-Fi diagnostics" in the boot menu: an app entry, after everything else (never ahead of Windows). */
STATIC
VOID
EFIAPI
AddDiagBootOption (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_LOADED_IMAGE_PROTOCOL          *Image;
  EFI_DEVICE_PATH_PROTOCOL           *FvPath;
  EFI_DEVICE_PATH_PROTOCOL           *Path;
  MEDIA_FW_VOL_FILEPATH_DEVICE_PATH  FileNode;
  EFI_BOOT_MANAGER_LOAD_OPTION       Option;
  EFI_BOOT_MANAGER_LOAD_OPTION       *Options;
  UINTN                              Count;
  VOID                               *VariableWrite;

  if (EFI_ERROR (gBS->LocateProtocol (&gEfiVariableWriteArchProtocolGuid, NULL, &VariableWrite))) {
    return;   // the protocol notify fires again once variables are writable
  }

  gBS->CloseEvent (Event);
  if (EFI_ERROR (gBS->HandleProtocol (gImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Image)) ||
      ((FvPath = DevicePathFromHandle (Image->DeviceHandle)) == NULL))
  {
    return;
  }

  EfiInitializeFwVolDevicepathNode (&FileNode, &mDiagGuid);
  Path = AppendDevicePathNode (FvPath, (EFI_DEVICE_PATH_PROTOCOL *)&FileNode);
  if (Path == NULL) {
    return;
  }

  if (!EFI_ERROR (EfiBootManagerInitializeLoadOption (&Option, LoadOptionNumberUnassigned, LoadOptionTypeBoot,
                    LOAD_OPTION_ACTIVE | LOAD_OPTION_CATEGORY_APP, L"Wi-Fi diagnostics", Path, NULL, 0)))
  {
    Options = EfiBootManagerGetLoadOptions (&Count, LoadOptionTypeBoot);
    if (EfiBootManagerFindLoadOption (&Option, Options, Count) < 0) {
      DEBUG ((DEBUG_ERROR, "NPCIE: boot entry \"Wi-Fi diagnostics\" %r\n",
        EfiBootManagerAddLoadOptionVariable (&Option, (UINTN)-1)));
    }

    EfiBootManagerFreeLoadOptions (Options, Count);
    EfiBootManagerFreeLoadOption (&Option);
  }

  FreePool (Path);
}

EFI_STATUS
EFIAPI
NegroniPcieEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_HANDLE  Handle = NULL;
  EFI_EVENT   Event;
  VOID        *Registration;

  Log ("Wi-Fi link %r\n", LinkUp ());
  gBS->InstallMultipleProtocolInterfaces (&Handle, &mLogGuid, &mLogProtocol, NULL);
  if (!EFI_ERROR (gBS->CreateEvent (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, AddDiagBootOption, NULL, &Event))) {
    gBS->RegisterProtocolNotify (&gEfiVariableWriteArchProtocolGuid, Event, &Registration);
    gBS->SignalEvent (Event);
  }

  return EFI_SUCCESS;
}
