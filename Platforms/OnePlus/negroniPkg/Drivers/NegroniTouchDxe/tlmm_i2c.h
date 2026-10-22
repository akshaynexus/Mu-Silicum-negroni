#ifndef NEGRONI_TLMM_I2C_H
#define NEGRONI_TLMM_I2C_H
#include "gpio_i2c.h"
#include "tlmm.h"
struct tlmm_i2c { volatile uint8_t *pins; };
static inline void ti_line(void *ctx, unsigned off, int release)
{
    struct tlmm_i2c *p = ctx;

    *(volatile uint32_t *)(p->pins + off + 4) = 0;
    *(volatile uint32_t *)(p->pins + off) = TLMM_FUNC(0) | TLMM_DRIVE_MA(2) |
        TLMM_PULL_UP | (release ? 0 : TLMM_OE);
    (void)*(volatile uint32_t *)(p->pins + off);
}
static inline void ti_sda(void *p, int r) { ti_line(p, 0, r); }
static inline void ti_scl(void *p, int r) { ti_line(p, 0x1000, r); }
static inline int ti_get_sda(void *p) { return *(volatile uint32_t *)(((struct tlmm_i2c *)p)->pins + 4) & 1; }
static inline int ti_get_scl(void *p) { return *(volatile uint32_t *)(((struct tlmm_i2c *)p)->pins + 0x1004) & 1; }
static inline void tlmm_i2c_init(struct gpio_i2c *g, struct tlmm_i2c *pins, void (*delay)(unsigned))
{
    g->ctx = pins; g->sda = ti_sda; g->scl = ti_scl;
    g->get_sda = ti_get_sda; g->get_scl = ti_get_scl; g->delay = delay;
    ti_sda(pins, 1); ti_scl(pins, 1); delay(5);
}
#endif
