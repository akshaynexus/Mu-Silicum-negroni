/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef NEGRONI_SPMI_KEYS_H_
#define NEGRONI_SPMI_KEYS_H_

#include <stdint.h>

enum {
  SK_VERSION_V7_MIN = 0x70000000u, SK_VERSION_V8_MIN = 0x80000000u,
  SK_FEATURES = 0x04, SK_FEATURES_APIDS_MASK = 0x7ffu,
  SK_APID_MAP = 0x2000,
  SK_OBS_EE_STRIDE = 0x8000, SK_OBS_APID_STRIDE = 0x20,
  SK_CMD = 0x00, SK_STATUS = 0x08, SK_RDATA0 = 0x18,
  SK_OP_EXT_READL = 1,
  SK_STATUS_DONE = 1u << 0, SK_STATUS_ERRORS = 0xeu,
  SK_POLLS = 100000,
  SK_PPID_PON = 0x013, SK_PPID_GPIO6 = 0x18d, SK_REG_RT_STS = 0x10,
  SK_PON_RESIN = 1u << 6, SK_PON_KPDPWR = 1u << 7,
  SK_KEY_POWER = 1u << 0, SK_KEY_VOLDOWN = 1u << 1, SK_KEY_VOLUP = 1u << 2
};

struct sk_arbiter {
  volatile uint32_t *core;
  volatile uint32_t *obs;
  uint8_t ee;
};

static inline int sk_find_apid (const struct sk_arbiter *a, uint32_t ppid, int *apid)
{
  uint32_t version = a->core[0], apids = a->core[SK_FEATURES / 4] & SK_FEATURES_APIDS_MASK;
  *apid = -1;
  if (version < SK_VERSION_V7_MIN || version >= SK_VERSION_V8_MIN) return 1;
  if (!apids || apids > 1024) return 2;
  for (uint32_t i = 0; i < apids; ++i) {
    uint32_t map = a->core[(SK_APID_MAP + 4 * i) / 4];
    if (map && ((map >> 8) & 0xfff) == ppid) { *apid = (int) i; break; }
  }
  return *apid < 0 ? 3 : 0;
}

static inline int sk_read8 (const struct sk_arbiter *a, int apid, uint8_t reg, uint8_t *value)
{
  volatile uint32_t *ch = a->obs + (SK_OBS_EE_STRIDE * a->ee + SK_OBS_APID_STRIDE * (uint32_t) apid) / 4;
  ch[SK_CMD / 4] = ((uint32_t) SK_OP_EXT_READL << 27) | ((uint32_t) reg << 4);
  for (unsigned n = 0; n < SK_POLLS; ++n) {
    uint32_t s = ch[SK_STATUS / 4];
    if (s & SK_STATUS_DONE) {
      if (s & SK_STATUS_ERRORS) return 2;
      *value = (uint8_t) ch[SK_RDATA0 / 4];
      return 0;
    }
  }
  return 1;
}

struct sk_keys { int pon, gpio; };

static inline int sk_open (const struct sk_arbiter *a, struct sk_keys *k)
{
  int rc = sk_find_apid (a, SK_PPID_PON, &k->pon);
  if (rc) return rc;
  if (sk_find_apid (a, SK_PPID_GPIO6, &k->gpio)) k->gpio = -1;
  return 0;
}

static inline int sk_sample (const struct sk_arbiter *a, const struct sk_keys *k, uint8_t *pressed)
{
  uint8_t v = 0;
  *pressed = 0;
  int rc = sk_read8 (a, k->pon, SK_REG_RT_STS, &v);
  if (rc) return rc;
  if (v & SK_PON_KPDPWR) *pressed |= SK_KEY_POWER;
  if (v & SK_PON_RESIN) *pressed |= SK_KEY_VOLUP;
  if (k->gpio >= 0) {
    rc = sk_read8 (a, k->gpio, SK_REG_RT_STS, &v);
    if (rc) { *pressed = 0; return rc; }
    if (!(v & 1)) *pressed |= SK_KEY_VOLDOWN;
  }
  return 0;
}

struct sk_filter { uint8_t last, stable; uint16_t held[3]; };

static inline uint8_t sk_step (struct sk_filter *f, uint8_t sample, uint16_t delay, uint16_t period)
{
  uint8_t out = 0;
  if (sample == f->last) {
    uint8_t fresh = (uint8_t) (sample & ~f->stable);
    f->stable = sample;
    out = fresh;
    for (unsigned i = 0; i < 3; ++i) {
      uint8_t bit = (uint8_t) (1u << i);
      if (!(sample & bit)) { f->held[i] = 0; continue; }
      if (fresh & bit) { f->held[i] = 0; continue; }
      ++f->held[i];
      if (bit != SK_KEY_POWER && f->held[i] >= delay && (f->held[i] - delay) % period == 0) out |= bit;
    }
  }
  f->last = sample;
  return out;
}

#endif
