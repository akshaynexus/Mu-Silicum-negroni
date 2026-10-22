/* SPDX-License-Identifier: BSD-2-Clause-Patent */

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/FdtLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/EFIClock.h>
#include <Protocol/AbsolutePointer.h>
#include <Protocol/LoadedImage.h>

#include "tlmm.h"
#include "geni_i2c.h"
#include "tlmm_i2c.h"
#include "syna_tcm.h"
#include "rpmh.h"
#include "EFII2C.h"
#include "NegroniTouchLog.h"

#define SE4_BASE   0x00990000UL
#define S3908_ADDR 0x4b

STATIC struct geni_i2c  mI2c = { (volatile uint8_t *)SE4_BASE, 0, 0 };
STATIC struct tcm       mTcm;
STATIC struct tlmm_i2c  mGpioPins = { (volatile uint8_t *)(TLMM_BASE + TLMM_CTL(NT_PIN_I2C_SDA)) };
STATIC struct gpio_i2c  mGpioI2c;
STATIC BOOLEAN         mUseGpio;
STATIC BOOLEAN          mReady;
STATIC CHAR8            mLog[NEGRONI_TOUCH_LOG_SIZE];
STATIC UINTN            mLogLen;
STATIC EFI_GUID         mLogProtocolGuid = NEGRONI_TOUCH_LOG_PROTOCOL_GUID;

STATIC EFI_STATUS EFIAPI Rerun (VOID);

STATIC NEGRONI_TOUCH_LOG_PROTOCOL  mLogProtocol = { mLog, Rerun };

STATIC CONST CHAR8 *mClocks[] = {
  "gcc_qupv3_wrap_0_m_ahb_clk",
  "gcc_qupv3_wrap_0_s_ahb_clk",
  "gcc_qupv3_wrap0_core_clk",
  "gcc_qupv3_wrap0_core_2x_clk",
  "gcc_qupv3_wrap0_s4_clk",
};

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
  DEBUG ((DEBUG_ERROR, "NTOUCH: %a", mLog + mLogLen));
  mLogLen += Len;
}

STATIC EFI_GUID               mQcomI2cGuid = QCOM_I2C_PROTOCOL_GUID;
STATIC EFI_QCOM_I2C_PROTOCOL  *mQcomI2c;
STATIC VOID                   *mQcomHandle;
STATIC UINT32                 mQcomInstance;
STATIC I2C_SLAVE_CONFIG       mQcomCfg = { I2C_FAST_MODE_FREQ_KHZ, S3908_ADDR, I2C, 2500, 0, 0 };

STATIC int QcomStatus (I2C_STATUS St)
{
  return St == I2C_SUCCESS ? GENI_I2C_OK : (St == I2C_ERROR_ADDR_NACK || St == I2C_ERROR_DATA_NACK) ? GENI_I2C_NACK :
         St == I2C_ERROR_TRANSFER_TIMEOUT ? GENI_I2C_TIMEOUT : GENI_I2C_BUS_ERR;
}

STATIC int QcomXfer (VOID *Handle, uint8_t *Buf, uint32_t Len, UINT32 Flags, I2C_STATUS *Raw)
{
  I2C_DESCRIPTOR  Desc = { Buf, Len, Flags };
  UINT32          Done = 0;
  I2C_STATUS      St   = mQcomI2c->Transfer (Handle, &mQcomCfg, &Desc, 1, NULL, NULL, 0, &Done);

  if (Raw != NULL) {
    *Raw = St;
  }

  if ((St == I2C_SUCCESS) && (Done != Len)) {
    return GENI_I2C_BUS_ERR;
  }
  return QcomStatus (St);
}

STATIC int QcomRead (void *Ctx, uint8_t *Buf, uint32_t Len)
{
  return QcomXfer (Ctx, Buf, Len, I2C_READ_TRANSFER, NULL);
}

#define DT_BLOB_BASE  0x80600000UL
#define DT_BLOB_SIZE  0x00040000UL

STATIC UINT32 Be32 (UINT32 V) { return SwapBytes32 (V); }

STATIC
VOID
__attribute__ ((unused))
PatchI2cConfig (
  VOID
  )
{
  for (UINTN Off = 0; Off + 0x40 < DT_BLOB_SIZE; Off += 8) {
    VOID    *Fdt = (VOID *)(DT_BLOB_BASE + Off);
    INT32   Node;
    UINT32  Size;

    if ((*(UINT32 *)Fdt != 0xEDFE0DD0) || (FdtCheckHeader (Fdt) != 0)) {
      continue;
    }

    Size = Be32 (((UINT32 *)Fdt)[1]);
    Node = FdtPathOffset (Fdt, "/soc/i2c");
    Log ("XBL config tree at 0x%lx (%u bytes): %a\n", (UINT64)(UINTN)Fdt, Size, Node >= 0 ? "has /soc/i2c" : "no /soc/i2c");
    if (Node < 0) {
      Off += (Size & ~7u);
      continue;
    }

    if (FdtSubnodeOffset (Fdt, Node, "i2c_device_config_5") >= 0) {
      Log ("  i2c_device_config_5: already present\n");
      return;
    }

    if (Off + Size + 4096 > DT_BLOB_SIZE) {
      Log ("  i2c_device_config_5: NO ROOM to grow the tree\n");
      return;
    }

    if (FdtOpenInto (Fdt, Fdt, Size + 4096) != 0) {
      Log ("  i2c_device_config_5: FdtOpenInto FAILED\n");
      return;
    }

    Node = FdtPathOffset (Fdt, "/soc/i2c");
    Node = FdtAddSubnode (Fdt, Node, "i2c_device_config_5");
    if (Node < 0) {
      Log ("  i2c_device_config_5: add FAILED [%d]\n", Node);
      return;
    }

    STATIC CONST struct { CONST CHAR8 *Name; UINT32 Val; } U32[] = {
      { "core_base_addr", 0x900000 }, { "common_base_addr", 0x9c0000 }, { "core_offset", 0x90000 },
      { "core_irq", 0 }, { "polled_mode", 1 }, { "min_data_length_for_dma", 0 },
      { "scl_encoding", 0x18111 }, { "sda_encoding", 0x18101 }, { "tcsr_reg_offset", 0 },
      { "tcsr_base_addr", 0x1fc0000 }, { "tcsr_reg_value", 0 }, { "common_clocks", 1 },
    };
    STATIC CONST struct { CONST CHAR8 *Name; UINT8 Val; } U8[] = {
      { "qupv3_instance", 0 }, { "core_index", 5 }, { "se_index", 4 }, { "gpi_index", 4 },
    };
    INT32  Err = 0;

    for (UINTN i = 0; i < ARRAY_SIZE (U8); i++) {
      Err |= FdtSetProp (Fdt, Node, U8[i].Name, &U8[i].Val, 1);
    }

    for (UINTN i = 0; i < ARRAY_SIZE (U32); i++) {
      UINT32  V = Be32 (U32[i].Val);
      Err |= FdtSetProp (Fdt, Node, U32[i].Name, &V, 4);
    }

    Err |= FdtSetProp (Fdt, Node, "se_clock", "gcc_qupv3_wrap0_s4_clk", sizeof ("gcc_qupv3_wrap0_s4_clk"));
    Log ("  i2c_device_config_5 (QUP0 SE4) added: %a\n", Err == 0 ? "OK" : "FAILED");
    return;
  }

  Log ("XBL config tree: NOT FOUND in DT BLOB\n");
}

STATIC int I2cRead (void *Ctx, uint8_t *Buf, uint32_t Len)
{
  if (mUseGpio) { return gpio_i2c_read (&mGpioI2c, S3908_ADDR, Buf, Len); }
  return mQcomHandle ? QcomXfer (mQcomHandle, Buf, Len, I2C_READ_TRANSFER, NULL) : geni_i2c_read (Ctx, S3908_ADDR, Buf, Len);
}

STATIC int I2cWrite (void *Ctx, const uint8_t *Buf, uint32_t Len)
{
  if (mUseGpio) { return gpio_i2c_write (&mGpioI2c, S3908_ADDR, Buf, Len); }
  return mQcomHandle ? QcomXfer (mQcomHandle, (uint8_t *)Buf, Len, I2C_WRITE_TRANSFER, NULL) : geni_i2c_write (Ctx, S3908_ADDR, Buf, Len);
}
STATIC void Wait1ms (void) { gBS->Stall (1000); }
STATIC void WaitUs (unsigned Us) { gBS->Stall (Us); }

STATIC CONST CHAR8 *
RpmhText (
  int  Rc
  )
{
  switch (Rc) {
    case 0: return "ON (OK)";
    case RH_E_GEOMETRY: return "FAILED (RPMh controller not recognised)";
    case RH_E_CMDDB: return "FAILED (cmd-db unreadable)";
    case RH_E_LOOKUP: return "FAILED (regulator not in cmd-db)";
    case RH_E_BUSY: return "FAILED (controller busy)";
    case RH_E_SEND: return "FAILED (write did not stick)";
    case RH_E_TIMEOUT: return "FAILED (timeout)";
    default: return "FAILED";
  }
}

STATIC CONST CHAR8 *
I2cText (
  int  Rc
  )
{
  switch (Rc) {
    case GENI_I2C_OK: return "answers (OK)";
    case GENI_I2C_NACK: return "NO ANSWER (NACK, unpowered or wrong address)";
    case GENI_I2C_BUS_ERR: return "BUS ERROR";
    case GENI_I2C_TIMEOUT: return "TIMEOUT (bus not clocking)";
    default: return "FAILED";
  }
}

STATIC
VOID
DumpTcs (
  VOID
  )
{
  struct rh_tcs_state  S;
  unsigned             T;

  Log ("rpmh irq_en 0x%x irq_status 0x%x\n", rh_rd (RH_TCS_BASE + RH_IRQ_EN), rh_rd (RH_TCS_BASE + RH_IRQ_STATUS));
  for (T = 0; T < RH_ACTIVE_N; T++) {
    rh_tcs_dump (T, &S);
    Log (
      "tcs%u idle %x ctl %x en %x wait %x | %x=%x st %x | %x=%x st %x | %x=%x st %x\n",
      T, S.idle, S.control, S.enable, S.wait,
      S.addr[0], S.data[0], S.status[0], S.addr[1], S.data[1], S.status[1], S.addr[2], S.data[2], S.status[2]
      );
  }
}

STATIC
VOID
PowerOn (
  CONST CHAR8  *Res,
  unsigned     Mv
  )
{
  struct rh_diag  D = { 0 };
  int             Rc = rh_vreg_enable (Res, Mv, WaitUs, &D);

  Log ("Power %a %u mV: %a [%d]\n", Res, Mv, RpmhText (Rc), Rc);
  if (Rc != 0) {
    Log ("  id 0x%x config 0x%x tcs %u ncpt %u db v%u addr 0x%x tcs%u\n", D.id, D.config, D.tcs_count, D.ncpt, D.db_version, D.address, D.tcs);
  }
  if (Rc != 0) {
    DumpTcs ();
  }
}

STATIC
VOID
EnableClocks (
  VOID
  )
{
  EFI_CLOCK_PROTOCOL  *Clock;
  EFI_STATUS          Status;
  UINTN               Id;
  UINTN               Index;

  Status = gBS->LocateProtocol (&gEfiClockProtocolGuid, NULL, (VOID **)&Clock);
  if (EFI_ERROR (Status)) {
    Log ("no clock protocol %r\n", Status);
    return;
  }

  for (Index = 0; Index < ARRAY_SIZE (mClocks); Index++) {
    Status = Clock->GetClockID (Clock, mClocks[Index], &Id);
    if (!EFI_ERROR (Status)) {
      Status = Clock->EnableClock (Clock, Id);
    }

    if (EFI_ERROR (Status)) {
      Log ("%a %r\n", mClocks[Index], Status);
    }
  }

  if (!EFI_ERROR (Clock->GetClockID (Clock, "gcc_qupv3_wrap0_s4_clk", &Id))) {
    UINT32  Hz = 0;

    Status = Clock->SetClockFreqHz (Clock, Id, 19200000, EFI_CLOCK_FREQUENCY_HZ_CLOSEST, &Hz);
    Log ("SE4 clock %u Hz %r\n", Hz, Status);
  }
}

STATIC
VOID
Probe (
  VOID
  )
{
  volatile UINT8  *Tlmm = (volatile UINT8 *)(UINTN)TLMM_BASE;
  int             Rc;
  UINTN           Try;

  mReady  = FALSE;
  mLogLen = 0;
  mLog[0] = 0;
  ZeroMem (&mTcm, sizeof (mTcm));
  mUseGpio = FALSE;
  Log ("Touch probe v3: GPIO I2C fallback\n");

  EnableClocks ();

  tlmm_out (Tlmm, NT_PIN_RESET, 0);
  PowerOn ("ldoc6", 1800);
  nt_pins_setup (Tlmm);
  PowerOn ("ldoc3", 3008);
  gBS->Stall (200 * 1000);
  tlmm_out (Tlmm, NT_PIN_RESET, 1);
  gBS->Stall (200 * 1000);
  Log ("pins: reset %d 1v8-en %d attn %d\n",
       (int)((*(volatile UINT32 *)(Tlmm + TLMM_IO (NT_PIN_RESET)) >> 1) & 1),
       (int)((*(volatile UINT32 *)(Tlmm + TLMM_IO (NT_PIN_1V8_EN)) >> 1) & 1),
       tlmm_in (Tlmm, NT_PIN_IRQ));

  Log ("I2C lines: %a [sda %d scl %d]\n",
       (tlmm_in (Tlmm, NT_PIN_I2C_SDA) && tlmm_in (Tlmm, NT_PIN_I2C_SCL)) ? "free (OK)" : "STUCK LOW",
       tlmm_in (Tlmm, NT_PIN_I2C_SDA), tlmm_in (Tlmm, NT_PIN_I2C_SCL));
  if (!tlmm_in (Tlmm, NT_PIN_I2C_SDA) || !tlmm_in (Tlmm, NT_PIN_I2C_SCL)) {
    Log ("I2C bus recovery: %a\n", nt_i2c_recover (Tlmm, WaitUs) ? "freed (OK)" : "still stuck");
  }

  nt_pins_setup_i2c (Tlmm);
  Rc = geni_i2c_init (&mI2c);
  Log ("SE4 proto %u fifo %u init %d fw 0x%x clk_cfg 0x%x ios 0x%x (bit0 sda bit1 scl)\n", geni_i2c_proto (&mI2c), mI2c.tx_depth, Rc,
       geni_rd (&mI2c, GENI_FW_REVISION_RO), geni_rd (&mI2c, GENI_SER_M_CLK_CFG), geni_rd (&mI2c, SE_GENI_IOS));
  if ((Rc != GENI_I2C_OK) && (Rc != GENI_I2C_FIFO_DISABLED)) {
    return;
  }

  {
    BOOLEAN  FifoOff = (geni_rd (&mI2c, GENI_IF_DISABLE_RO) & FIFO_IF_DISABLE) != 0;

    Log ("Transfer mode: %a\n", FifoOff ? "GPIO open-drain (FIFO disabled)" : "FIFO");
    if (FifoOff) {

      tlmm_i2c_init (&mGpioI2c, &mGpioPins, WaitUs);
      mUseGpio = TRUE;
    }
    if (FifoOff && !mUseGpio && (mQcomHandle == NULL)) {
      if (EFI_ERROR (gBS->LocateProtocol (&mQcomI2cGuid, NULL, (VOID **)&mQcomI2c))) {
        Log ("Qualcomm I2C protocol: NOT FOUND\n");
        return;
      }

      for (UINT32 Inst = 1; Inst <= 24 && mQcomHandle == NULL; Inst++) {
        VOID        *H = NULL;
        struct tcm  Candidate = { 0 };
        I2C_STATUS  St;
        int         ReadRc;

        DEBUG ((DEBUG_ERROR, "NTOUCH: trying I2C instance %u\n", Inst));
        St = mQcomI2c->Open (Inst, &H);

        if (St != I2C_SUCCESS) {
          continue;
        }

        Candidate.io.ctx = H;
        Candidate.io.read = QcomRead;
        ReadRc = tcm_read_message (&Candidate);
        if ((ReadRc >= 0) && (Candidate.header[0] == TCM_MARKER)) {
          mQcomHandle   = H;
          mQcomInstance = Inst;
        } else {
          mQcomI2c->Close (H);
        }

        Log ("  I2C instance %u: frame rc %d bus %d hdr %02x %02x %02x %02x\n", Inst,
             ReadRc, Candidate.transport_error, Candidate.header[0], Candidate.header[1],
             Candidate.header[2], Candidate.header[3]);
      }

      Log ("Qualcomm I2C: %a instance %u\n", mQcomHandle ? "using (OK)" : "NO INSTANCE REACHES THE CHIP", mQcomInstance);
      if (mQcomHandle == NULL) {
        return;
      }
    }
  }

  mTcm.io.ctx   = &mI2c;
  mTcm.io.read  = I2cRead;
  mTcm.io.write = I2cWrite;

  for (Try = 0; Try < 50; Try++) {
    Rc = tcm_read_message (&mTcm);
    if (Rc <= 0) {
      break;
    }

    gBS->Stall (10 * 1000);
  }

  Log ("TouchComm message: %a [%d code 0x%x len %u]\n", Rc == 0 ? "received (OK)" : Rc > 0 ? "none waiting" : "FAILED (no 0xA5 marker / bus error)",
       Rc, mTcm.code, mTcm.payload_len);
  Log ("wire hdr %02x %02x %02x %02x bus %d (%a)\n", mTcm.header[0], mTcm.header[1],
       mTcm.header[2], mTcm.header[3], mTcm.transport_error, I2cText (mTcm.transport_error));
  if ((Rc == 0) && (mTcm.code == TCM_REPORT_IDENTIFY) && (mTcm.payload_len >= 2)) {
    Log ("identify version %u mode %u\n", mTcm.payload[0], mTcm.payload[1]);
  }

  Rc = tcm_get_report_config (&mTcm, Wait1ms);
  Log ("Touch report config: %a [%d, %u bytes]\n", Rc == 0 ? "OK" : "FAILED", Rc, mTcm.config_len);
  Log ("config bus %d hdr %02x %02x %02x %02x\n", mTcm.transport_error,
       mTcm.header[0], mTcm.header[1], mTcm.header[2], mTcm.header[3]);
  Log ("Interrupt line: %a\n", tlmm_in (Tlmm, NT_PIN_IRQ) ? "idle (high)" : "message waiting (low)");
  mReady = (Rc == 0);
  Log ("Touch pointer (front page): %a\n", mReady ? "active (OK)" : "off (no report config)");

}

STATIC EFI_ABSOLUTE_POINTER_MODE   mPointerMode = {
  0, 0, 0, TCM_PANEL_MAX_X, TCM_PANEL_MAX_Y, 0, 0
};
STATIC EFI_ABSOLUTE_POINTER_STATE  mPointerState;
STATIC BOOLEAN                     mPointerChanged;
STATIC EFI_ABSOLUTE_POINTER_PROTOCOL  mPointer;

STATIC
EFI_STATUS
EFIAPI
PointerReset (
  IN EFI_ABSOLUTE_POINTER_PROTOCOL  *This,
  IN BOOLEAN                        ExtendedVerification
  )
{
  ZeroMem (&mPointerState, sizeof (mPointerState));
  mPointerChanged = FALSE;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
PointerGetState (
  IN     EFI_ABSOLUTE_POINTER_PROTOCOL  *This,
  OUT    EFI_ABSOLUTE_POINTER_STATE     *State
  )
{
  if (State == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (!mPointerChanged) {
    return EFI_NOT_READY;
  }

  CopyMem (State, &mPointerState, sizeof (*State));
  mPointerChanged = FALSE;
  return EFI_SUCCESS;
}

STATIC
VOID
EFIAPI
PointerWaitForInput (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  if (mPointerChanged) {
    gBS->SignalEvent (Event);
  }
}

STATIC
VOID
EFIAPI
PointerPoll (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  volatile UINT8  *Tlmm = (volatile UINT8 *)(UINTN)TLMM_BASE;
  UINTN           Msg;
  UINTN           Obj;
  BOOLEAN         Down;

  if (!mReady) {
    return;
  }

  for (Msg = 0; Msg < 4 && !tlmm_in (Tlmm, NT_PIN_IRQ); Msg++) {
    if ((tcm_read_message (&mTcm) != 0) || (mTcm.code != TCM_REPORT_TOUCH) || (tcm_parse_touch (&mTcm) < 0)) {
      continue;
    }

    Down = FALSE;
    for (Obj = 0; Obj < TCM_MAX_OBJECTS; Obj++) {
      if (mTcm.obj[Obj].status != 0) {
        mPointerState.CurrentX = MIN (mTcm.obj[Obj].x, TCM_PANEL_MAX_X);
        mPointerState.CurrentY = MIN (mTcm.obj[Obj].y, TCM_PANEL_MAX_Y);
        Down = TRUE;
        break;
      }
    }

    if (Down || (mPointerState.ActiveButtons != 0)) {
      mPointerState.ActiveButtons = Down ? EFI_ABSP_TouchActive : 0;
      mPointerChanged = TRUE;
    }
  }
}

STATIC EFI_HANDLE  mPointerHandle;

STATIC
VOID
EFIAPI
ConnectPointer (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  gBS->CloseEvent (Event);
  DEBUG ((DEBUG_ERROR, "NTOUCH: connect pointer %r\n", gBS->ConnectController (mPointerHandle, NULL, NULL, TRUE)));
}

STATIC
EFI_STATUS
InstallPointer (
  VOID
  )
{
  STATIC struct {
    VENDOR_DEVICE_PATH          Vendor;
    EFI_DEVICE_PATH_PROTOCOL    End;
  } Path = {
    {
      { HARDWARE_DEVICE_PATH, HW_VENDOR_DP, { sizeof (VENDOR_DEVICE_PATH), 0 } },
      { 0x4b2e9d71, 0x0c6a, 0x4f83, { 0x9e, 0x15, 0x7a, 0x3d, 0x2c, 0x8b, 0x5f, 0x61 } }
    },
    { END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, { sizeof (EFI_DEVICE_PATH_PROTOCOL), 0 } }
  };
  EFI_EVENT   Timer;
  EFI_EVENT   EndOfDxe;
  EFI_STATUS  Status;

  mPointerMode.Attributes = 0;
  mPointer.Reset    = PointerReset;
  mPointer.GetState = PointerGetState;
  mPointer.Mode     = &mPointerMode;
  Status = gBS->CreateEvent (EVT_NOTIFY_WAIT, TPL_NOTIFY, PointerWaitForInput, NULL, &mPointer.WaitForInput);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, PointerPoll, NULL, &Timer);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  gBS->SetTimer (Timer, TimerPeriodic, 10 * 10000);
  Status = gBS->InstallMultipleProtocolInterfaces (&mPointerHandle, &gEfiDevicePathProtocolGuid, &Path,
                  &gEfiAbsolutePointerProtocolGuid, &mPointer, NULL);
  DEBUG ((DEBUG_ERROR, "NTOUCH: absolute pointer %r\n", Status));
  if (!EFI_ERROR (Status)) {
    gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, ConnectPointer, NULL, &gEfiEndOfDxeEventGroupGuid, &EndOfDxe);
  }

  return Status;
}

STATIC
EFI_STATUS
EFIAPI
Rerun (
  VOID
  )
{
  EFI_TPL  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);

  Probe ();
  gBS->RestoreTPL (OldTpl);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
NegroniTouchDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_HANDLE  Handle = NULL;

  Probe ();
  gBS->InstallMultipleProtocolInterfaces (&Handle, &mLogProtocolGuid, &mLogProtocol, NULL);
  InstallPointer ();
  return EFI_SUCCESS;
}
