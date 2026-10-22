/* SPDX-License-Identifier: BSD-2-Clause-Patent */

#ifndef NEGRONI_TOUCH_LOG_H_
#define NEGRONI_TOUCH_LOG_H_

#define NEGRONI_TOUCH_LOG_PROTOCOL_GUID \
  { 0x6c1f3a52, 0x9d4e, 0x4b7a, { 0x8e, 0x21, 0x5f, 0x0c, 0x93, 0x7d, 0x4a, 0xb6 } }

#define NEGRONI_TOUCH_DIAG_FILE_GUID \
  { 0x2d8e5b19, 0x47c3, 0x4f60, { 0xa1, 0x9e, 0x3b, 0x7d, 0x0f, 0x52, 0xc8, 0x14 } }

#define NEGRONI_TOUCH_LOG_SIZE  8192

typedef struct _NEGRONI_TOUCH_LOG_PROTOCOL NEGRONI_TOUCH_LOG_PROTOCOL;

typedef
EFI_STATUS
(EFIAPI *NEGRONI_TOUCH_RERUN)(
  VOID
  );

struct _NEGRONI_TOUCH_LOG_PROTOCOL {
  CONST CHAR8           *Log;
  NEGRONI_TOUCH_RERUN   Rerun;
};

#endif
