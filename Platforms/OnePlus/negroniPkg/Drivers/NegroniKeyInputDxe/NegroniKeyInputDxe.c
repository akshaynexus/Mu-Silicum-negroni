/* SPDX-License-Identifier: BSD-2-Clause-Patent */

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/DevicePathLib.h>
#include <Library/UefiLib.h>
#include <Protocol/DevicePath.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/SimpleTextInEx.h>
#include <Guid/EventGroup.h>
#include <Guid/GlobalVariable.h>

#include "NegroniSpmiKeys.h"

#define POLL_MS         20
#define REPEAT_DELAY    25
#define REPEAT_PERIOD   7
#define QUEUE_SIZE      16
#define MAX_FAILURES    50

#define SPMI_CORE_BASE    0x0C400000UL
#define SPMI_OBSRVR_BASE  0x0C440000UL

typedef struct {
  VENDOR_DEVICE_PATH          Vendor;
  EFI_DEVICE_PATH_PROTOCOL    End;
} NEGRONI_KEY_DEVICE_PATH;

STATIC NEGRONI_KEY_DEVICE_PATH  mPath = {
  {
    { HARDWARE_DEVICE_PATH, HW_VENDOR_DP, { sizeof (VENDOR_DEVICE_PATH), 0 } },

    { 0xD7F58A0E, 0xBED2, 0x4B5A, { 0xBB, 0x43, 0x8A, 0xB2, 0x3D, 0xD0, 0xE2, 0xB0 } }
  },
  { END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, { sizeof (EFI_DEVICE_PATH_PROTOCOL), 0 } }
};

STATIC struct sk_arbiter  mArbiter;
STATIC struct sk_keys     mKeys;
STATIC struct sk_filter   mFilter;
STATIC EFI_INPUT_KEY      mQueue[QUEUE_SIZE];
STATIC UINTN              mHead, mCount;
STATIC UINTN              mFailures;
STATIC EFI_EVENT          mTimer, mExit;
STATIC EFI_HANDLE         mHandle;

#define MAX_NOTIFY  8
typedef struct {
  BOOLEAN                  Used;
  EFI_KEY_DATA             Key;
  EFI_KEY_NOTIFY_FUNCTION  Function;
} KEY_NOTIFY;
STATIC KEY_NOTIFY  mNotify[MAX_NOTIFY];

STATIC
EFI_STATUS
EFIAPI
KeyReset (
  IN EFI_SIMPLE_TEXT_INPUT_PROTOCOL  *This,
  IN BOOLEAN                         ExtendedVerification
  )
{
  EFI_TPL  Tpl = gBS->RaiseTPL (TPL_NOTIFY);

  mHead  = 0;
  mCount = 0;
  gBS->RestoreTPL (Tpl);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
KeyRead (
  IN  EFI_SIMPLE_TEXT_INPUT_PROTOCOL  *This,
  OUT EFI_INPUT_KEY                   *Key
  )
{
  EFI_TPL  Tpl = gBS->RaiseTPL (TPL_NOTIFY);

  if (mCount == 0) {
    gBS->RestoreTPL (Tpl);
    return EFI_NOT_READY;
  }

  *Key  = mQueue[mHead];
  mHead = (mHead + 1) % QUEUE_SIZE;
  mCount--;
  gBS->RestoreTPL (Tpl);
  return EFI_SUCCESS;
}

STATIC EFI_SIMPLE_TEXT_INPUT_PROTOCOL  mInput = { KeyReset, KeyRead, NULL };

STATIC
EFI_STATUS
EFIAPI
KeyResetEx (
  IN EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  IN BOOLEAN                            ExtendedVerification
  )
{
  return KeyReset (&mInput, ExtendedVerification);
}

STATIC
EFI_STATUS
EFIAPI
KeyReadEx (
  IN  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  OUT EFI_KEY_DATA                       *KeyData
  )
{
  if (KeyData == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  KeyData->KeyState.KeyShiftState  = 0;
  KeyData->KeyState.KeyToggleState = 0;
  return KeyRead (&mInput, &KeyData->Key);
}

STATIC
EFI_STATUS
EFIAPI
KeySetState (
  IN EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  IN EFI_KEY_TOGGLE_STATE               *KeyToggleState
  )
{
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
KeyRegisterNotify (
  IN  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  IN  EFI_KEY_DATA                       *KeyData,
  IN  EFI_KEY_NOTIFY_FUNCTION            KeyNotificationFunction,
  OUT VOID                               **NotifyHandle
  )
{
  if ((KeyData == NULL) || (KeyNotificationFunction == NULL) || (NotifyHandle == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  for (UINTN Index = 0; Index < MAX_NOTIFY; Index++) {
    if (!mNotify[Index].Used) {
      mNotify[Index].Used     = TRUE;
      mNotify[Index].Key      = *KeyData;
      mNotify[Index].Function = KeyNotificationFunction;
      *NotifyHandle           = &mNotify[Index];
      return EFI_SUCCESS;
    }
  }

  return EFI_OUT_OF_RESOURCES;
}

STATIC
EFI_STATUS
EFIAPI
KeyUnregisterNotify (
  IN EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  IN VOID                               *NotificationHandle
  )
{
  KEY_NOTIFY  *Entry = NotificationHandle;

  if ((Entry < &mNotify[0]) || (Entry >= &mNotify[MAX_NOTIFY]) || !Entry->Used) {
    return EFI_INVALID_PARAMETER;
  }

  Entry->Used = FALSE;
  return EFI_SUCCESS;
}

STATIC EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  mInputEx = {
  KeyResetEx, KeyReadEx, NULL, KeySetState, KeyRegisterNotify, KeyUnregisterNotify
};

STATIC
VOID
EFIAPI
KeyWait (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  if (mCount != 0) {
    gBS->SignalEvent (Event);
  }
}

STATIC
VOID
Push (
  IN UINT16  Scan,
  IN CHAR16  Unicode
  )
{
  if (mCount == QUEUE_SIZE) {
    return;
  }

  mQueue[(mHead + mCount) % QUEUE_SIZE].ScanCode    = Scan;
  mQueue[(mHead + mCount) % QUEUE_SIZE].UnicodeChar = Unicode;
  mCount++;
  gBS->SignalEvent (mInput.WaitForKey);

  for (UINTN Index = 0; Index < MAX_NOTIFY; Index++) {
    if (mNotify[Index].Used && (mNotify[Index].Key.Key.ScanCode == Scan) &&
        (mNotify[Index].Key.Key.UnicodeChar == Unicode))
    {
      EFI_KEY_DATA  Data;

      Data.Key.ScanCode                = Scan;
      Data.Key.UnicodeChar             = Unicode;
      Data.KeyState.KeyShiftState      = 0;
      Data.KeyState.KeyToggleState     = 0;
      mNotify[Index].Function (&Data);
    }
  }
}

STATIC
VOID
EFIAPI
Poll (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  UINT8  Pressed;
  UINT8  Out;

  if (sk_sample (&mArbiter, &mKeys, &Pressed) != 0) {
    if (++mFailures >= MAX_FAILURES) {
      gBS->SetTimer (mTimer, TimerCancel, 0);
      DEBUG ((DEBUG_ERROR, "NegroniKeyInput: too many failed samples, polling stopped\n"));
    }

    return;
  }

  mFailures = 0;
  Out       = sk_step (&mFilter, Pressed, REPEAT_DELAY, REPEAT_PERIOD);
  if (Out & SK_KEY_VOLUP) {
    Push (SCAN_UP, 0);
  }

  if (Out & SK_KEY_VOLDOWN) {
    Push (SCAN_DOWN, 0);
  }

  if (Out & SK_KEY_POWER) {
    Push (SCAN_NULL, CHAR_CARRIAGE_RETURN);
  }
}

STATIC
VOID
EFIAPI
OnExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  gBS->SetTimer (mTimer, TimerCancel, 0);
}

STATIC
EFI_STATUS
AddToConIn (
  VOID
  )
{
  EFI_STATUS  Status;
  UINTN       OldSize, OurSize, NewSize;
  UINT32      Attributes;
  UINT8       *Old, *New;

  OurSize = GetDevicePathSize ((EFI_DEVICE_PATH_PROTOCOL *)&mPath);
  OldSize = 0;
  Status  = gRT->GetVariable (EFI_CON_IN_VARIABLE_NAME, &gEfiGlobalVariableGuid, &Attributes, &OldSize, NULL);
  if (Status == EFI_NOT_FOUND) {
    return gRT->SetVariable (EFI_CON_IN_VARIABLE_NAME, &gEfiGlobalVariableGuid, EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS, OurSize, &mPath);
  }

  if (Status != EFI_BUFFER_TOO_SMALL) {
    return Status;
  }

  Old = AllocatePool (OldSize + OurSize);
  if (Old == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = gRT->GetVariable (EFI_CON_IN_VARIABLE_NAME, &gEfiGlobalVariableGuid, &Attributes, &OldSize, Old);
  if (!EFI_ERROR (Status)) {
    EFI_DEVICE_PATH_PROTOCOL  *Instance;
    UINTN                     Offset;

    for (Offset = 0; Offset < OldSize;) {
      Instance = (EFI_DEVICE_PATH_PROTOCOL *)(Old + Offset);
      if (CompareMem (Instance, &mPath, sizeof (VENDOR_DEVICE_PATH)) == 0) {
        FreePool (Old);
        return EFI_SUCCESS;
      }

      Offset += GetDevicePathSize (Instance);
      if (Offset >= OldSize) {
        break;
      }
    }

    New     = Old;
    NewSize = OldSize - sizeof (EFI_DEVICE_PATH_PROTOCOL);
    CopyMem (New + NewSize, &mPath, OurSize);
    NewSize += OurSize;
    Status   = gRT->SetVariable (EFI_CON_IN_VARIABLE_NAME, &gEfiGlobalVariableGuid, Attributes, NewSize, New);
  }

  FreePool (Old);
  return Status;
}

EFI_STATUS
EFIAPI
NegroniKeyInputDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  int         Rc;

  mArbiter.core = (volatile UINT32 *)SPMI_CORE_BASE;
  mArbiter.obs  = (volatile UINT32 *)SPMI_OBSRVR_BASE;
  mArbiter.ee   = 0;

  Rc = sk_open (&mArbiter, &mKeys);
  if (Rc != 0) {
    DEBUG ((DEBUG_ERROR, "NegroniKeyInput: arbiter lookup failed (%d), no keys\n", Rc));
    return EFI_UNSUPPORTED;
  }

  DEBUG ((DEBUG_INFO, "NegroniKeyInput: PON apid %d, volume up apid %d\n", mKeys.pon, mKeys.gpio));

  Status = gBS->CreateEvent (EVT_NOTIFY_WAIT, TPL_NOTIFY, KeyWait, NULL, &mInput.WaitForKey);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  mInputEx.WaitForKeyEx = mInput.WaitForKey;

  Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, Poll, NULL, &mTimer);
  if (!EFI_ERROR (Status)) {
    Status = gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, OnExitBootServices, NULL, &gEfiEventExitBootServicesGuid, &mExit);
  }

  if (!EFI_ERROR (Status)) {
    Status = gBS->InstallMultipleProtocolInterfaces (
                    &mHandle,
                    &gEfiDevicePathProtocolGuid,
                    &mPath,
                    &gEfiSimpleTextInProtocolGuid,
                    &mInput,
                    &gEfiSimpleTextInputExProtocolGuid,
                    &mInputEx,
                    NULL
                    );
  }

  if (!EFI_ERROR (Status)) {
    Status = gBS->SetTimer (mTimer, TimerPeriodic, EFI_TIMER_PERIOD_MILLISECONDS (POLL_MS));
  }

  if (!EFI_ERROR (Status)) {
    EFI_STATUS  Listed = AddToConIn ();

    DEBUG ((DEBUG_INFO, "NegroniKeyInput: ConIn %r\n", Listed));

    gBS->ConnectController (mHandle, NULL, NULL, TRUE);
  }

  return Status;
}
