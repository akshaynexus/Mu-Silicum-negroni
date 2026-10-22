#ifndef NEGRONI_GENI_I2C_H
#define NEGRONI_GENI_I2C_H

#include <stdint.h>

#define GENI_FORCE_DEFAULT_REG		0x020
#define GENI_IF_DISABLE_RO		0x064
#define FIFO_IF_DISABLE			(1u << 0)
#define SE_GENI_STATUS			0x040
#define GENI_SER_M_CLK_CFG		0x048
#define GENI_FW_REVISION_RO		0x068
#define SE_GENI_CLK_SEL			0x07c
#define SE_GENI_BYTE_GRAN		0x254
#define SE_GENI_DMA_MODE_EN		0x258
#define SE_GENI_TX_PACKING_CFG0		0x260
#define SE_GENI_TX_PACKING_CFG1		0x264
#define SE_I2C_TX_TRANS_LEN		0x26c
#define SE_I2C_RX_TRANS_LEN		0x270
#define SE_I2C_SCL_COUNTERS		0x278
#define SE_GENI_RX_PACKING_CFG0		0x284
#define SE_GENI_RX_PACKING_CFG1		0x288
#define SE_GENI_M_CMD0			0x600
#define SE_GENI_M_CMD_CTRL_REG		0x604
#define SE_GENI_M_IRQ_STATUS		0x610
#define SE_GENI_M_IRQ_EN		0x614
#define SE_GENI_M_IRQ_CLEAR		0x618
#define SE_GENI_S_IRQ_CLEAR		0x648
#define SE_GENI_TX_FIFOn		0x700
#define SE_GENI_RX_FIFOn		0x780
#define SE_GENI_TX_FIFO_STATUS		0x800
#define SE_GENI_RX_FIFO_STATUS		0x804
#define SE_GENI_TX_WATERMARK_REG	0x80c
#define SE_GENI_RX_WATERMARK_REG	0x810
#define SE_GENI_RX_RFR_WATERMARK_REG	0x814
#define SE_HW_PARAM_0			0xe24
#define SE_GENI_IOS			0x908

#define GENI_SE_I2C			3

#define M_CMD_DONE_EN			(1u << 0)
#define M_CMD_OVERRUN_EN		(1u << 1)
#define M_ILLEGAL_CMD_EN		(1u << 2)
#define M_CMD_FAILURE_EN		(1u << 3)
#define M_CMD_CANCEL_EN			(1u << 4)
#define M_CMD_ABORT_EN			(1u << 5)
#define M_GP_IRQ_1_EN			(1u << 10)
#define M_GP_IRQ_3_EN			(1u << 12)
#define M_GP_IRQ_4_EN			(1u << 13)
#define M_RX_FIFO_WATERMARK_EN		(1u << 26)
#define M_RX_FIFO_LAST_EN		(1u << 27)
#define M_TX_FIFO_WATERMARK_EN		(1u << 30)
#define SE_I2C_ERR	(M_CMD_OVERRUN_EN | M_ILLEGAL_CMD_EN | M_CMD_FAILURE_EN | \
			 M_GP_IRQ_1_EN | M_GP_IRQ_3_EN | M_GP_IRQ_4_EN)

#define M_GENI_CMD_ABORT		(1u << 1)
#define M_GENI_CMD_CANCEL		(1u << 2)
#define RX_FIFO_WC_MSK			0x01ffffffu
#define RX_LAST				(1u << 31)
#define RX_LAST_BYTE_VALID_SHFT		28

#define I2C_WRITE			0x1
#define I2C_READ			0x2
#define M_OPCODE_SHFT			27
#define STOP_STRETCH			(1u << 2)
#define SLV_ADDR_SHFT			9

#define GENI_I2C_400K_DIV		2
#define GENI_I2C_400K_SCL		((5u << 20) | (12u << 10) | 24u)

enum geni_i2c_status {
	GENI_I2C_OK = 0,
	GENI_I2C_NOT_I2C = -1,
	GENI_I2C_NACK = -2,
	GENI_I2C_BUS_ERR = -3,
	GENI_I2C_TIMEOUT = -4,
	GENI_I2C_FIFO_DISABLED = -5,
};

struct geni_i2c {
	volatile uint8_t *base;
	uint32_t polls;
	uint32_t tx_depth;
};

static inline uint32_t geni_rd(const struct geni_i2c *g, uint32_t off)
{
	return *(volatile uint32_t *)(g->base + off);
}

static inline void geni_wr(const struct geni_i2c *g, uint32_t off, uint32_t v)
{
	*(volatile uint32_t *)(g->base + off) = v;
}

static inline uint32_t geni_i2c_proto(const struct geni_i2c *g)
{
	return (geni_rd(g, GENI_FW_REVISION_RO) >> 8) & 0xff;
}

static inline void geni_i2c_packing(const struct geni_i2c *g)
{
	uint32_t cfg[4], cfg0, cfg1;

	for (int i = 0; i < 4; i++)
		cfg[i] = ((uint32_t)(i * 8) << 5) | (7u << 1);
	cfg[3] |= 1;
	cfg0 = cfg[0] | (cfg[1] << 10);
	cfg1 = cfg[2] | (cfg[3] << 10);
	geni_wr(g, SE_GENI_TX_PACKING_CFG0, cfg0);
	geni_wr(g, SE_GENI_TX_PACKING_CFG1, cfg1);
	geni_wr(g, SE_GENI_RX_PACKING_CFG0, cfg0);
	geni_wr(g, SE_GENI_RX_PACKING_CFG1, cfg1);
	geni_wr(g, SE_GENI_BYTE_GRAN, 0);
}

static inline int geni_i2c_init(struct geni_i2c *g)
{
	if (!g->polls)
		g->polls = 2000000;
	if (geni_i2c_proto(g) != GENI_SE_I2C)
		return GENI_I2C_NOT_I2C;

	if (geni_rd(g, GENI_IF_DISABLE_RO) & FIFO_IF_DISABLE)
		return GENI_I2C_FIFO_DISABLED;
	geni_wr(g, SE_GENI_M_IRQ_CLEAR, 0xffffffffu);
	geni_wr(g, SE_GENI_S_IRQ_CLEAR, 0xffffffffu);
	geni_wr(g, SE_GENI_M_IRQ_EN, geni_rd(g, SE_GENI_M_IRQ_EN) | M_CMD_DONE_EN |
		M_TX_FIFO_WATERMARK_EN | M_RX_FIFO_WATERMARK_EN | M_RX_FIFO_LAST_EN | SE_I2C_ERR);
	geni_wr(g, SE_GENI_DMA_MODE_EN, geni_rd(g, SE_GENI_DMA_MODE_EN) & ~1u);
	geni_i2c_packing(g);

	{
		uint32_t depth = (geni_rd(g, SE_HW_PARAM_0) >> 16) & 0x3f;

		if (!depth)
			depth = 16;
		g->tx_depth = depth;
		geni_wr(g, SE_GENI_RX_WATERMARK_REG, depth - 1);
		geni_wr(g, SE_GENI_RX_RFR_WATERMARK_REG, depth);
	}
	geni_wr(g, SE_GENI_CLK_SEL, 0);
	geni_wr(g, GENI_SER_M_CLK_CFG, (GENI_I2C_400K_DIV << 4) | 1u);
	geni_wr(g, SE_I2C_SCL_COUNTERS, GENI_I2C_400K_SCL);
	return GENI_I2C_OK;
}

static inline int geni_i2c_error(uint32_t st)
{
	if (st & M_GP_IRQ_1_EN)
		return GENI_I2C_NACK;
	return GENI_I2C_BUS_ERR;
}

static inline void geni_i2c_abort(const struct geni_i2c *g)
{
	geni_wr(g, SE_GENI_M_CMD_CTRL_REG, M_GENI_CMD_ABORT);
	for (uint32_t n = 0; n < g->polls; n++)
		if (geni_rd(g, SE_GENI_M_IRQ_STATUS) & M_CMD_ABORT_EN)
			break;
	geni_wr(g, SE_GENI_M_IRQ_CLEAR, 0xffffffffu);
}

static inline int geni_i2c_write(const struct geni_i2c *g, uint8_t addr, const uint8_t *buf, uint32_t len)
{
	uint32_t sent = 0;

	geni_wr(g, SE_GENI_M_IRQ_CLEAR, 0xffffffffu);
	geni_wr(g, SE_I2C_TX_TRANS_LEN, len);
	geni_wr(g, SE_GENI_M_CMD0, (I2C_WRITE << M_OPCODE_SHFT) | ((uint32_t)addr << SLV_ADDR_SHFT));
	if (len)
		geni_wr(g, SE_GENI_TX_WATERMARK_REG, 1);
	for (uint32_t n = 0; n < g->polls; n++) {
		uint32_t st = geni_rd(g, SE_GENI_M_IRQ_STATUS);

		if (st & SE_I2C_ERR) {
			geni_i2c_abort(g);
			return geni_i2c_error(st);
		}

		if (sent < len && (st & M_TX_FIFO_WATERMARK_EN)) {
			uint32_t w = 0;
			for (uint32_t i = 0; i < 4 && sent < len; i++)
				w |= (uint32_t)buf[sent++] << (8 * i);
			geni_wr(g, SE_GENI_TX_FIFOn, w);
			if (sent == len)
				geni_wr(g, SE_GENI_TX_WATERMARK_REG, 0);
			geni_wr(g, SE_GENI_M_IRQ_CLEAR, M_TX_FIFO_WATERMARK_EN);
			continue;
		}
		if (st & M_CMD_DONE_EN) {
			geni_wr(g, SE_GENI_M_IRQ_CLEAR, st);
			return GENI_I2C_OK;
		}
	}
	geni_i2c_abort(g);
	return GENI_I2C_TIMEOUT;
}

static inline int geni_i2c_read(const struct geni_i2c *g, uint8_t addr, uint8_t *buf, uint32_t len)
{
	uint32_t got = 0;

	geni_wr(g, SE_GENI_M_IRQ_CLEAR, 0xffffffffu);
	geni_wr(g, SE_I2C_RX_TRANS_LEN, len);
	geni_wr(g, SE_GENI_M_CMD0, (I2C_READ << M_OPCODE_SHFT) | ((uint32_t)addr << SLV_ADDR_SHFT));
	for (uint32_t n = 0; n < g->polls; n++) {
		uint32_t st = geni_rd(g, SE_GENI_M_IRQ_STATUS);

		if (st & SE_I2C_ERR) {
			geni_i2c_abort(g);
			return geni_i2c_error(st);
		}
		if (st & (M_RX_FIFO_WATERMARK_EN | M_RX_FIFO_LAST_EN)) {
			uint32_t words = geni_rd(g, SE_GENI_RX_FIFO_STATUS) & RX_FIFO_WC_MSK;
			while (words--) {
				uint32_t w = geni_rd(g, SE_GENI_RX_FIFOn);
				for (uint32_t i = 0; i < 4 && got < len; i++)
					buf[got++] = (uint8_t)(w >> (8 * i));
			}
			geni_wr(g, SE_GENI_M_IRQ_CLEAR, st & (M_RX_FIFO_WATERMARK_EN | M_RX_FIFO_LAST_EN));
		}
		if (st & M_CMD_DONE_EN) {
			geni_wr(g, SE_GENI_M_IRQ_CLEAR, st);
			return got == len ? GENI_I2C_OK : GENI_I2C_BUS_ERR;
		}
	}
	geni_i2c_abort(g);
	return GENI_I2C_TIMEOUT;
}

#endif
