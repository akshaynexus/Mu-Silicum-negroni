#ifndef NEGRONI_GPIO_I2C_H
#define NEGRONI_GPIO_I2C_H
#include <stdint.h>
struct gpio_i2c {
    void *ctx;
    void (*sda)(void *, int);
    void (*scl)(void *, int);
    int (*get_sda)(void *);
    int (*get_scl)(void *);
    void (*delay)(unsigned);
};

static inline int gi_high(struct gpio_i2c *g)
{
    g->scl(g->ctx, 1);
    for (unsigned n = 0; n < 1000; n++) {
        if (g->get_scl(g->ctx)) { g->delay(5); return 0; }
        g->delay(1);
    }
    return -4;
}
static inline void gi_low(struct gpio_i2c *g)
{
    g->scl(g->ctx, 0);
    g->delay(5);
}
static inline int gi_stop(struct gpio_i2c *g)
{
    gi_low(g);
    g->sda(g->ctx, 0);
    g->delay(5);
    int rc = gi_high(g);
    g->sda(g->ctx, 1);
    g->delay(5);
    return rc;
}
static inline int gi_byte(struct gpio_i2c *g, uint8_t v)
{
    for (unsigned i = 0; i < 8; i++) {
        g->sda(g->ctx, (v & 0x80) != 0);
        g->delay(5);
        int rc = gi_high(g);
        if (rc) return rc;
        gi_low(g);
        v <<= 1;
    }
    g->sda(g->ctx, 1);
    g->delay(5);
    int rc = gi_high(g);
    if (rc) return rc;
    int nack = g->get_sda(g->ctx);
    gi_low(g);
    return nack ? -2 : 0;
}
static inline int gi_start(struct gpio_i2c *g, uint8_t addr, int read)
{
    g->sda(g->ctx, 1);
    int rc = gi_high(g);
    if (rc) return rc;
    if (!g->get_sda(g->ctx)) return -3;
    g->sda(g->ctx, 0);
    g->delay(5);
    gi_low(g);
    return gi_byte(g, (uint8_t)((addr << 1) | read));
}
static inline int gpio_i2c_write(struct gpio_i2c *g, uint8_t addr, const uint8_t *buf, uint32_t len)
{
    int rc = gi_start(g, addr, 0);
    for (uint32_t i = 0; !rc && i < len; i++) rc = gi_byte(g, buf[i]);
    int stop = gi_stop(g);
    return rc ? rc : stop;
}
static inline int gpio_i2c_read(struct gpio_i2c *g, uint8_t addr, uint8_t *buf, uint32_t len)
{
    int rc = gi_start(g, addr, 1);
    for (uint32_t i = 0; !rc && i < len; i++) {
        uint8_t v = 0;
        g->sda(g->ctx, 1);
        for (unsigned bit = 0; bit < 8; bit++) {
            rc = gi_high(g);
            if (rc) break;
            v = (uint8_t)((v << 1) | !!g->get_sda(g->ctx));
            gi_low(g);
        }
        if (rc) break;
        buf[i] = v;
        g->sda(g->ctx, i + 1 == len);
        g->delay(5);
        rc = gi_high(g);
        gi_low(g);
        g->sda(g->ctx, 1);
    }
    int stop = gi_stop(g);
    return rc ? rc : stop;
}
#endif
