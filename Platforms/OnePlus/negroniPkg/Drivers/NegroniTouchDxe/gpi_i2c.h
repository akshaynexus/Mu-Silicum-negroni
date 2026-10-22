#ifndef NEGRONI_GPI_I2C_H
#define NEGRONI_GPI_I2C_H

#include <stdint.h>
#include "geni_i2c.h"

#define GPI_BASE		0x00900000u
#define GPI_EE_BASE		(GPI_BASE - 0x10000u)
#define GPI_CH_CNTXT(n, k)	(0x20000u + 0x4000u * (n) + 0x80u * (k))
#define GPI_EV_CNTXT(n)		(0x21000u + 0x4000u * (n))
#define GPI_CH_DB(n, k)		(0x22000u + 0x4000u * (n) + 0x8u * (k))
#define GPI_EV_DB(n)		(0x22100u + 0x4000u * (n))
#define GPI_CH_CMD(n)		(0x23008u + 0x4000u * (n))
#define GPI_EV_CMD(n)		(0x23010u + 0x4000u * (n))
#define GPI_CH_QOS(n, k)	(0x2005Cu + 0x4000u * (n) + 0x80u * (k))
#define GPI_CH_SCRATCH(n, k, i)	(0x20060u + 0x4000u * (n) + 0x80u * (k) + 4u * (i))

#define CNTXT_0_CONFIG		0x00
#define CNTXT_1_R_LENGTH	0x04
#define CNTXT_2_RING_BASE_LSB	0x08
#define CNTXT_3_RING_BASE_MSB	0x0C
#define CNTXT_4_RING_RP_LSB	0x10
#define CNTXT_8_RING_INT_MOD	0x20
#define CNTXT_10_RING_MSI_LSB	0x28
#define CNTXT_11_RING_MSI_MSB	0x2C
#define CNTXT_12_RING_RP_UPD_LSB 0x30
#define CNTXT_13_RING_RP_UPD_MSB 0x34

#define GPI_CMD_ALLOCATE	0x00
#define GPI_CMD_START		0x01
#define GPI_CMD_STOP		0x02
#define GPI_CMD_RESET		0x09
#define GPI_CMD_DE_ALLOC	0x0A

#define GPI_STATE(cfg)		(((cfg) >> 20) & 0xfu)
#define GPI_ST_NOT_ALLOC	0
#define GPI_ST_ALLOCATED	1
#define GPI_ST_STARTED		2
#define GPI_ST_STOPPED		3

#define GPI_EL_SIZE		16u
#define GPI_TRES		64u
#define GPI_EVS			(GPI_TRES << 2)
#define GPI_PROTO_I2C		3u
#define GPI_SEID		4u

#define TRE_TYPE_DMA		0x10u
#define TRE_TYPE_GO		0x20u
#define TRE_TYPE_CONFIG0	0x22u
#define TRE_CHAIN		(1u << 0)
#define TRE_IEOT		(1u << 9)
#define TRE_LINK		(1u << 11)
#define TRE_TYPE(t)		((uint32_t)(t) << 16)

#define EV_XFER_COMPLETE	0x22u
#define TCE_SUCCESS		1u
#define TCE_EOT			2u
#define TCE_UNEXP_ERR		16u

#define SE_GSI_EVENT_EN		0xe18
#define SE_IRQ_EN		0xe1c

#define GPI_EV_OFF		0x0000u
#define GPI_TX_OFF		0x1000u
#define GPI_RX_OFF		0x1400u
#define GPI_BUF_OFF		0x2000u
#define GPI_BUF_SIZE		0x1000u
#define GPI_I2C_MEM		(GPI_BUF_OFF + GPI_BUF_SIZE)

#define GPI_E_NO_GPII		-10
#define GPI_E_CMD		-11

struct gpi_i2c {
	volatile uint8_t *gpi;
	volatile uint8_t *se;
	uint8_t *mem;
	uint64_t mem_pa;
	uint32_t n;
	uint32_t tx_wp, rx_wp, ev_rp;
	uint32_t configured;
	uint32_t last_ev[4];
	uint32_t polls;
	void (*udelay)(unsigned);
};

static inline uint32_t gpi_rd(const struct gpi_i2c *g, uint32_t off)
{
	uint32_t v = *(volatile uint32_t *)(g->gpi + off);
	__asm__ volatile("dmb oshld" ::: "memory");
	return v;
}

static inline void gpi_wr(const struct gpi_i2c *g, uint32_t off, uint32_t v)
{
	__asm__ volatile("dmb oshst" ::: "memory");
	*(volatile uint32_t *)(g->gpi + off) = v;
}

static inline int gpi_wait_state(const struct gpi_i2c *g, uint32_t cntxt, uint32_t want)
{
	for (uint32_t i = 0; i < 10000; i++) {
		if (GPI_STATE(gpi_rd(g, cntxt + CNTXT_0_CONFIG)) == want)
			return 0;
		g->udelay(10);
	}
	return GPI_E_CMD;
}

static inline int gpi_ch_cmd(const struct gpi_i2c *g, uint32_t k, uint32_t cmd, uint32_t want)
{
	gpi_wr(g, GPI_CH_CMD(g->n), (cmd << 24) | k);
	return gpi_wait_state(g, GPI_CH_CNTXT(g->n, k), want);
}

static inline int gpi_ev_cmd(const struct gpi_i2c *g, uint32_t cmd, uint32_t want)
{
	gpi_wr(g, GPI_EV_CMD(g->n), cmd << 24);
	return gpi_wait_state(g, GPI_EV_CNTXT(g->n), want);
}

static inline void gpi_release(struct gpi_i2c *g)
{
	for (uint32_t k = 0; k < 2; k++) {
		uint32_t st = GPI_STATE(gpi_rd(g, GPI_CH_CNTXT(g->n, k)));
		if (st == GPI_ST_STARTED)
			gpi_ch_cmd(g, k, GPI_CMD_STOP, GPI_ST_STOPPED);
		if (st != GPI_ST_NOT_ALLOC) {
			gpi_ch_cmd(g, k, GPI_CMD_RESET, GPI_ST_ALLOCATED);
			gpi_ch_cmd(g, k, GPI_CMD_DE_ALLOC, GPI_ST_NOT_ALLOC);
		}
	}
	if (GPI_STATE(gpi_rd(g, GPI_EV_CNTXT(g->n))) != GPI_ST_NOT_ALLOC) {
		gpi_ev_cmd(g, GPI_CMD_RESET, GPI_ST_ALLOCATED);
		gpi_ev_cmd(g, GPI_CMD_DE_ALLOC, GPI_ST_NOT_ALLOC);
	}
}

static inline void gpi_ring_ctx(const struct gpi_i2c *g, uint32_t cntxt, uint32_t cfg, uint32_t len, uint64_t pa)
{
	gpi_wr(g, cntxt + CNTXT_0_CONFIG, cfg);
	gpi_wr(g, cntxt + CNTXT_1_R_LENGTH, len);
	gpi_wr(g, cntxt + CNTXT_2_RING_BASE_LSB, (uint32_t)pa);
	gpi_wr(g, cntxt + CNTXT_3_RING_BASE_MSB, (uint32_t)(pa >> 32));
}

static inline int gpi_i2c_init(struct gpi_i2c *g)
{
	int rc = GPI_E_NO_GPII;

	if (!g->polls)
		g->polls = 200000;
	for (uint32_t i = 0; i < GPI_I2C_MEM; i++)
		g->mem[i] = 0;

	*(volatile uint32_t *)(g->se + SE_GENI_M_IRQ_CLEAR) = 0xffffffffu;
	*(volatile uint32_t *)(g->se + SE_GENI_S_IRQ_CLEAR) = 0xffffffffu;
	*(volatile uint32_t *)(g->se + SE_IRQ_EN) = 0;
	*(volatile uint32_t *)(g->se + SE_GENI_M_IRQ_EN) &= ~(M_CMD_DONE_EN | M_TX_FIFO_WATERMARK_EN |
							       M_RX_FIFO_WATERMARK_EN | M_RX_FIFO_LAST_EN);
	*(volatile uint32_t *)(g->se + SE_GENI_DMA_MODE_EN) = 1;
	*(volatile uint32_t *)(g->se + SE_GSI_EVENT_EN) |= 0xfu;

	for (g->n = 1; g->n <= 6; g->n++) {
		gpi_release(g);
		if (GPI_STATE(gpi_rd(g, GPI_CH_CNTXT(g->n, 0))) == GPI_ST_NOT_ALLOC &&
		    GPI_STATE(gpi_rd(g, GPI_CH_CNTXT(g->n, 1))) == GPI_ST_NOT_ALLOC &&
		    GPI_STATE(gpi_rd(g, GPI_EV_CNTXT(g->n))) == GPI_ST_NOT_ALLOC) {
			rc = 0;
			break;
		}
	}
	if (rc)
		return rc;

	rc = gpi_ev_cmd(g, GPI_CMD_ALLOCATE, GPI_ST_ALLOCATED);
	if (rc)
		return rc;
	uint32_t ev = GPI_EV_CNTXT(g->n);
	gpi_ring_ctx(g, ev, (GPI_EL_SIZE << 24) | (1u << 16) | 2u, GPI_EVS * GPI_EL_SIZE, g->mem_pa + GPI_EV_OFF);
	gpi_wr(g, GPI_EV_DB(g->n) + 4, (uint32_t)((g->mem_pa + GPI_EV_OFF) >> 32));
	gpi_wr(g, ev + CNTXT_8_RING_INT_MOD, 0);
	gpi_wr(g, ev + CNTXT_10_RING_MSI_LSB, 0);
	gpi_wr(g, ev + CNTXT_11_RING_MSI_MSB, 0);
	gpi_wr(g, ev + CNTXT_12_RING_RP_UPD_LSB, 0);
	gpi_wr(g, ev + CNTXT_13_RING_RP_UPD_MSB, 0);
	g->ev_rp = 0;

	gpi_wr(g, GPI_EV_DB(g->n), (uint32_t)(g->mem_pa + GPI_EV_OFF + (GPI_EVS - 1) * GPI_EL_SIZE));

	for (uint32_t k = 0; k < 2; k++) {
		uint64_t pa = g->mem_pa + (k ? GPI_RX_OFF : GPI_TX_OFF);
		rc = gpi_ch_cmd(g, k, GPI_CMD_ALLOCATE, GPI_ST_ALLOCATED);
		if (rc)
			return rc;
		gpi_ring_ctx(g, GPI_CH_CNTXT(g->n, k), (GPI_EL_SIZE << 24) | ((k ? 0u : 1u) << 3) | 2u,
			     GPI_TRES * GPI_EL_SIZE, pa);
		gpi_wr(g, GPI_CH_DB(g->n, k) + 4, (uint32_t)(pa >> 32));
		gpi_wr(g, GPI_CH_SCRATCH(g->n, k, 0), ((1u - k) << 16) | (GPI_PROTO_I2C << 4) | GPI_SEID);
		gpi_wr(g, GPI_CH_SCRATCH(g->n, k, 1), 0);
		gpi_wr(g, GPI_CH_SCRATCH(g->n, k, 2), 0);
		gpi_wr(g, GPI_CH_SCRATCH(g->n, k, 3), 0);
		gpi_wr(g, GPI_CH_QOS(g->n, k), 1);
	}
	g->tx_wp = g->rx_wp = 0;
	g->configured = 0;
	for (uint32_t k = 0; k < 2; k++) {
		rc = gpi_ch_cmd(g, k, GPI_CMD_START, GPI_ST_STARTED);
		if (rc)
			return rc;
	}
	return 0;
}

static inline void gpi_put_tre(struct gpi_i2c *g, uint32_t k, uint32_t d0, uint32_t d1, uint32_t d2, uint32_t d3)
{
	uint32_t *wp = k ? &g->rx_wp : &g->tx_wp;
	volatile uint32_t *tre = (volatile uint32_t *)(g->mem + (k ? GPI_RX_OFF : GPI_TX_OFF) + *wp * GPI_EL_SIZE);
	tre[0] = d0;
	tre[1] = d1;
	tre[2] = d2;
	tre[3] = d3;
	*wp = (*wp + 1) % GPI_TRES;
}

static inline void gpi_ring_db(struct gpi_i2c *g, uint32_t k)
{
	uint32_t wp = k ? g->rx_wp : g->tx_wp;
	__asm__ volatile("dsb sy" ::: "memory");
	gpi_wr(g, GPI_CH_DB(g->n, k), (uint32_t)(g->mem_pa + (k ? GPI_RX_OFF : GPI_TX_OFF) + wp * GPI_EL_SIZE));
}

static inline void gpi_config0(struct gpi_i2c *g)
{
	if (g->configured)
		return;
	gpi_put_tre(g, 0, 12u | (5u << 8) | (24u << 16) | (1u << 24) | (1u << 25), 0, 2u,
		    TRE_TYPE(TRE_TYPE_CONFIG0) | TRE_CHAIN);
	g->configured = 1;
}

static inline int gpi_wait(struct gpi_i2c *g, uint32_t k)
{
	uint64_t ev_pa = g->mem_pa + GPI_EV_OFF;

	for (uint32_t n = 0; n < g->polls; n++) {
		uint32_t rp_pa = gpi_rd(g, GPI_EV_CNTXT(g->n) + CNTXT_4_RING_RP_LSB);
		uint32_t hw = ((uint32_t)(rp_pa - (uint32_t)ev_pa) / GPI_EL_SIZE) % GPI_EVS;

		while (g->ev_rp != hw) {
			volatile uint32_t *e = (volatile uint32_t *)(g->mem + GPI_EV_OFF + g->ev_rp * GPI_EL_SIZE);
			uint32_t type = (e[3] >> 16) & 0xff, chid = e[3] >> 24, code = e[2] >> 24;
			for (int i = 0; i < 4; i++)
				g->last_ev[i] = e[i];
			g->ev_rp = (g->ev_rp + 1) % GPI_EVS;

			gpi_wr(g, GPI_EV_DB(g->n), (uint32_t)(ev_pa + ((g->ev_rp + GPI_EVS - 1) % GPI_EVS) * GPI_EL_SIZE));
			if (type != EV_XFER_COMPLETE || chid != k)
				continue;
			if (code == TCE_EOT || code == TCE_SUCCESS)
				return GENI_I2C_OK;

			return (e[3] & M_GP_IRQ_1_EN) ? GENI_I2C_NACK : GENI_I2C_BUS_ERR;
		}
		g->udelay(1);
	}
	return GENI_I2C_TIMEOUT;
}

static inline int gpi_i2c_write(struct gpi_i2c *g, uint8_t addr, const uint8_t *buf, uint32_t len)
{
	if (len > GPI_BUF_SIZE)
		return GENI_I2C_BUS_ERR;
	for (uint32_t i = 0; i < len; i++)
		g->mem[GPI_BUF_OFF + i] = buf[i];
	gpi_config0(g);
	gpi_put_tre(g, 0, I2C_WRITE | ((uint32_t)addr << 8), 0, 0, TRE_TYPE(TRE_TYPE_GO) | TRE_CHAIN);
	gpi_put_tre(g, 0, (uint32_t)(g->mem_pa + GPI_BUF_OFF), (uint32_t)((g->mem_pa + GPI_BUF_OFF) >> 32),
		    len, TRE_TYPE(TRE_TYPE_DMA) | TRE_IEOT);
	gpi_ring_db(g, 0);
	return gpi_wait(g, 0);
}

static inline int gpi_i2c_read(struct gpi_i2c *g, uint8_t addr, uint8_t *buf, uint32_t len)
{
	int rc;

	if (len > GPI_BUF_SIZE)
		return GENI_I2C_BUS_ERR;

	gpi_put_tre(g, 1, (uint32_t)(g->mem_pa + GPI_BUF_OFF), (uint32_t)((g->mem_pa + GPI_BUF_OFF) >> 32),
		    len, TRE_TYPE(TRE_TYPE_DMA) | TRE_IEOT);
	gpi_ring_db(g, 1);
	gpi_config0(g);
	gpi_put_tre(g, 0, I2C_READ | ((uint32_t)addr << 8), 0, len, TRE_TYPE(TRE_TYPE_GO) | TRE_LINK);
	gpi_ring_db(g, 0);
	rc = gpi_wait(g, 1);
	if (rc == GENI_I2C_OK)
		for (uint32_t i = 0; i < len; i++)
			buf[i] = g->mem[GPI_BUF_OFF + i];
	return rc;
}

#endif
