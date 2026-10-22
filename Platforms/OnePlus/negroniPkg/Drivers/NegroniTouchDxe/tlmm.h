#ifndef NEGRONI_TLMM_H
#define NEGRONI_TLMM_H

#include <stdint.h>

#define TLMM_BASE		0x0F100000u
#define TLMM_CTL(n)		((uint32_t)(n) * 0x1000u)
#define TLMM_IO(n)		((uint32_t)(n) * 0x1000u + 4u)

#define TLMM_PULL_NONE		0u
#define TLMM_PULL_DOWN		1u
#define TLMM_PULL_UP		3u
#define TLMM_FUNC(f)		((uint32_t)(f) << 2)
#define TLMM_DRIVE_MA(ma)	((uint32_t)((ma) / 2 - 1) << 6)
#define TLMM_OE			(1u << 9)
#define TLMM_IN			(1u << 0)
#define TLMM_OUT		(1u << 1)

#define NT_PIN_I2C_SDA		16
#define NT_PIN_I2C_SCL		17
#define NT_PIN_RESET		20
#define NT_PIN_IRQ		21
#define NT_PIN_1V8_EN		186
#define NT_FUNC_QUP4		1

static inline void tlmm_cfg(volatile uint8_t *tlmm, unsigned pin, uint32_t cfg)
{
	*(volatile uint32_t *)(tlmm + TLMM_CTL(pin)) = cfg;
}

static inline void tlmm_out(volatile uint8_t *tlmm, unsigned pin, int high)
{
	*(volatile uint32_t *)(tlmm + TLMM_IO(pin)) = high ? TLMM_OUT : 0;
}

static inline int tlmm_in(volatile uint8_t *tlmm, unsigned pin)
{
	return (int)(*(volatile uint32_t *)(tlmm + TLMM_IO(pin)) & TLMM_IN);
}

static inline void nt_pins_setup_i2c(volatile uint8_t *tlmm)
{
	tlmm_cfg(tlmm, NT_PIN_I2C_SDA, TLMM_FUNC(NT_FUNC_QUP4) | TLMM_DRIVE_MA(2) | TLMM_PULL_UP);
	tlmm_cfg(tlmm, NT_PIN_I2C_SCL, TLMM_FUNC(NT_FUNC_QUP4) | TLMM_DRIVE_MA(2) | TLMM_PULL_UP);
}

static inline void nt_pins_setup(volatile uint8_t *tlmm)
{
	nt_pins_setup_i2c(tlmm);
	tlmm_cfg(tlmm, NT_PIN_IRQ, TLMM_FUNC(0) | TLMM_DRIVE_MA(8) | TLMM_PULL_NONE);
	tlmm_out(tlmm, NT_PIN_RESET, 0);
	tlmm_cfg(tlmm, NT_PIN_RESET, TLMM_FUNC(0) | TLMM_DRIVE_MA(8) | TLMM_PULL_NONE | TLMM_OE);
	tlmm_out(tlmm, NT_PIN_1V8_EN, 1);
	tlmm_cfg(tlmm, NT_PIN_1V8_EN, TLMM_FUNC(0) | TLMM_DRIVE_MA(2) | TLMM_PULL_NONE | TLMM_OE);
}

static inline int nt_i2c_recover(volatile uint8_t *tlmm, void (*udelay)(unsigned))
{
	uint32_t in = TLMM_FUNC(0) | TLMM_DRIVE_MA(2) | TLMM_PULL_UP;

	tlmm_cfg(tlmm, NT_PIN_I2C_SDA, in);
	tlmm_cfg(tlmm, NT_PIN_I2C_SCL, in);
	udelay(10);
	for (int i = 0; i < 9 && !tlmm_in(tlmm, NT_PIN_I2C_SDA); i++) {
		tlmm_out(tlmm, NT_PIN_I2C_SCL, 0);
		tlmm_cfg(tlmm, NT_PIN_I2C_SCL, in | TLMM_OE);
		udelay(5);
		tlmm_cfg(tlmm, NT_PIN_I2C_SCL, in);
		udelay(5);
	}

	tlmm_out(tlmm, NT_PIN_I2C_SDA, 0);
	tlmm_cfg(tlmm, NT_PIN_I2C_SDA, in | TLMM_OE);
	udelay(5);
	tlmm_cfg(tlmm, NT_PIN_I2C_SDA, in);
	udelay(5);
	return tlmm_in(tlmm, NT_PIN_I2C_SDA);
}

#endif
