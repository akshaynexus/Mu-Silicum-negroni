#ifndef NEGRONI_RPMH_H
#define NEGRONI_RPMH_H

#include <stdint.h>

#define RH_DRV_BASE	0x17a20000u
#define RH_TCS_BASE	(RH_DRV_BASE + 0xd00u)
#define RH_DRV		2u
#define RH_ACTIVE_N	3u
#define RH_CMD_DB	0x80860000u
#define RH_CMD_DB_SIZE	0x20000u
#define RH_TCS_STRIDE	0x2a0u
#define RH_CMD_STRIDE	0x14u
#define RH_IRQ_EN	0x00u
#define RH_IRQ_STATUS	0x04u
#define RH_IRQ_CLEAR	0x08u
#define RH_WAIT		0x10u
#define RH_CONTROL	0x14u
#define RH_STATUS	0x18u
#define RH_ENABLE	0x1cu
#define RH_MSGID	0x30u
#define RH_ADDR		0x34u
#define RH_DATA		0x38u
#define RH_CMD_STATUS	0x3cu
#define RH_AMC_EN	(1u << 16)
#define RH_TRIGGER	(1u << 24)
#define RH_ISSUED	(1u << 8)
#define RH_COMPLETE	(1u << 16)
#define RH_WRITE_ACK	(8u | (1u << 8) | (1u << 16))
#define RH_DB_MAGIC	0x0c0330dbu
#define RH_DB_HEADER	144u
#define RH_DB_ENTRY	24u
#define RH_VRM		4u
#define RH_PMIC5_HPM	7u
#define RH_TIMEOUT_US	100000u

#define RH_E_GEOMETRY	-1
#define RH_E_CMDDB	-2
#define RH_E_LOOKUP	-3
#define RH_E_BUSY	-4
#define RH_E_SEND	-5
#define RH_E_TIMEOUT	-6

struct rh_diag {
	uint32_t id, config, tcs_count, ncpt, db_version, address, tcs, irq, idle, status[3];
};

static inline uint32_t rh_rd(uint32_t pa)
{
	uint32_t v = *(volatile uint32_t *)(uintptr_t)pa;
	__asm__ volatile("dmb oshld" ::: "memory");
	return v;
}

static inline void rh_wr(uint32_t pa, uint32_t v)
{
	__asm__ volatile("dmb oshst" ::: "memory");
	*(volatile uint32_t *)(uintptr_t)pa = v;
	__asm__ volatile("dsb osh" ::: "memory");
}

static inline int rh_sync(uint32_t pa, uint32_t v, void (*udelay)(unsigned))
{
	rh_wr(pa, v);
	for (unsigned i = 0; i < 1000; i++) {
		if (rh_rd(pa) == v)
			return 1;
		udelay(1);
	}
	return 0;
}

static inline uint16_t rh_db16(uint32_t off)
{
	uint32_t v = rh_rd(RH_CMD_DB + (off & ~3u));
	return (uint16_t)(v >> (8u * (off & 3u)));
}

static inline int rh_lookup(const char *res, struct rh_diag *d)
{
	uint32_t key[2] = { 0, 0 };
	unsigned matches = 0;

	for (unsigned i = 0; i < 8 && res[i]; i++)
		key[i / 4] |= (uint32_t)(uint8_t)res[i] << (8u * (i % 4));
	for (unsigned i = 0; i < 8; i++) {
		uint32_t h = 8u + 16u * i;
		unsigned slave = rh_db16(h);
		if (!slave)
			break;
		uint32_t entries = RH_DB_HEADER + rh_db16(h + 2);
		unsigned count = rh_db16(h + 6);
		if (entries + count * RH_DB_ENTRY > RH_CMD_DB_SIZE)
			return RH_E_CMDDB;
		for (unsigned j = 0; j < count; j++) {
			uint32_t e = RH_CMD_DB + entries + RH_DB_ENTRY * j;
			if (rh_rd(e) != key[0] || rh_rd(e + 4) != key[1])
				continue;
			uint32_t addr = rh_rd(e + 16);
			if (slave != RH_VRM || (addr >> 16) != RH_VRM || (addr & 15u))
				return RH_E_LOOKUP;
			d->address = addr;
			matches++;
		}
	}
	return matches == 1 ? 0 : RH_E_LOOKUP;
}

static inline int rh_tcs_cmds_done(unsigned t, unsigned ncpt)
{
	uint32_t b = RH_TCS_BASE + t * RH_TCS_STRIDE;
	uint32_t enabled = rh_rd(b + RH_ENABLE);
	for (unsigned c = 0; c < ncpt; c++) {
		uint32_t st = rh_rd(b + RH_CMD_STATUS + c * RH_CMD_STRIDE);
		if ((enabled & (1u << c)) && (st == 0xffffffffu || (st & (RH_ISSUED | RH_COMPLETE)) != (RH_ISSUED | RH_COMPLETE)))
			return 0;
	}
	return 1;
}

static inline int rh_tcs_free(unsigned t, unsigned ncpt)
{
	uint32_t b = RH_TCS_BASE + t * RH_TCS_STRIDE;
	uint32_t idle = rh_rd(b + RH_STATUS);
	return idle != 0 && idle != 0xffffffffu && !(rh_rd(b + RH_CONTROL) & RH_TRIGGER) && rh_tcs_cmds_done(t, ncpt);
}

struct rh_tcs_state {
	uint32_t idle, control, enable, wait, addr[3], data[3], status[3];
};

static inline void rh_tcs_dump(unsigned t, struct rh_tcs_state *s)
{
	uint32_t b = RH_TCS_BASE + t * RH_TCS_STRIDE;
	s->idle = rh_rd(b + RH_STATUS);
	s->control = rh_rd(b + RH_CONTROL);
	s->enable = rh_rd(b + RH_ENABLE);
	s->wait = rh_rd(b + RH_WAIT);
	for (unsigned c = 0; c < 3; c++) {
		s->addr[c] = rh_rd(b + RH_ADDR + c * RH_CMD_STRIDE);
		s->data[c] = rh_rd(b + RH_DATA + c * RH_CMD_STRIDE);
		s->status[c] = rh_rd(b + RH_CMD_STATUS + c * RH_CMD_STRIDE);
	}
}

static inline int rh_vreg_enable(const char *res, unsigned mv, void (*udelay)(unsigned), struct rh_diag *d)
{
	d->id = rh_rd(RH_DRV_BASE);
	d->config = rh_rd(RH_DRV_BASE + 0x0c);
	d->tcs_count = (d->config >> (6u * RH_DRV)) & 0x3fu;
	d->ncpt = (d->config >> 27) & 0x1fu;
	unsigned major = (d->id >> 16) & 0xffu;
	if ((major != 1 && major != 2) || d->config == 0xffffffffu || d->tcs_count < RH_ACTIVE_N ||
	    d->tcs_count > 0x3000u / RH_TCS_STRIDE || d->ncpt < 3 || d->ncpt > 16)
		return RH_E_GEOMETRY;

	d->db_version = rh_rd(RH_CMD_DB);
	if (rh_rd(RH_CMD_DB + 4) != RH_DB_MAGIC)
		return RH_E_CMDDB;
	int rc = rh_lookup(res, d);
	if (rc)
		return rc;

	int chosen = -1;
	for (int t = RH_ACTIVE_N - 1; t >= 0; t--) {
		uint32_t b = RH_TCS_BASE + (uint32_t)t * RH_TCS_STRIDE;
		uint32_t enabled = rh_rd(b + RH_ENABLE);
		int done = rh_tcs_cmds_done((unsigned)t, d->ncpt);

		for (unsigned c = 0; c < d->ncpt; c++)
			if (!done && (enabled & (1u << c)) && (rh_rd(b + RH_ADDR + c * RH_CMD_STRIDE) & ~15u) == d->address)
				return RH_E_BUSY;
		if (chosen < 0 && rh_tcs_free((unsigned)t, d->ncpt))
			chosen = t;
	}
	if (chosen < 0)
		return RH_E_BUSY;

	unsigned t = (unsigned)chosen;
	uint32_t b = RH_TCS_BASE + t * RH_TCS_STRIDE, bit = 1u << t;
	uint32_t irq_en = rh_rd(RH_TCS_BASE + RH_IRQ_EN);
	d->tcs = t;
	if (!rh_sync(RH_TCS_BASE + RH_IRQ_EN, irq_en & ~bit, udelay))
		return RH_E_SEND;

	if (!rh_sync(b + RH_CONTROL, 0, udelay) || !rh_sync(b + RH_ENABLE, 0, udelay) || !rh_sync(b + RH_WAIT, 0, udelay))
		return RH_E_SEND;
	rh_wr(RH_TCS_BASE + RH_IRQ_CLEAR, bit);

	const uint32_t offsets[3] = { 0, 8, 4 };
	const uint32_t values[3] = { mv, RH_PMIC5_HPM, 1 };
	for (unsigned c = 0; c < 3; c++) {
		uint32_t cmd = b + c * RH_CMD_STRIDE;
		if (!rh_sync(cmd + RH_MSGID, RH_WRITE_ACK, udelay) ||
		    !rh_sync(cmd + RH_ADDR, d->address + offsets[c], udelay) ||
		    !rh_sync(cmd + RH_DATA, values[c], udelay))
			return RH_E_SEND;
	}
	if (!rh_sync(b + RH_WAIT, 7, udelay) || !rh_sync(b + RH_ENABLE, 7, udelay) ||
	    !rh_sync(b + RH_CONTROL, 0, udelay) || !rh_sync(b + RH_CONTROL, RH_AMC_EN, udelay))
		return RH_E_SEND;
	rh_wr(b + RH_CONTROL, RH_AMC_EN | RH_TRIGGER);

	int done = 0, progress = 0;
	for (unsigned us = 0; us < RH_TIMEOUT_US; us++) {
		int complete = 1;
		d->irq = rh_rd(RH_TCS_BASE + RH_IRQ_STATUS);
		d->idle = rh_rd(b + RH_STATUS);
		for (unsigned c = 0; c < 3; c++) {
			d->status[c] = rh_rd(b + RH_CMD_STATUS + c * RH_CMD_STRIDE);
			complete &= d->status[c] != 0xffffffffu &&
				    (d->status[c] & (RH_ISSUED | RH_COMPLETE)) == (RH_ISSUED | RH_COMPLETE);
		}
		progress |= d->idle == 0 || !complete;
		if (d->idle && d->idle != 0xffffffffu && ((d->irq & bit) || progress) && complete) {
			done = 1;
			break;
		}
		udelay(1);
	}
	if (!done)
		return RH_E_TIMEOUT;

	if (!rh_sync(b + RH_CONTROL, RH_AMC_EN, udelay) || !rh_sync(b + RH_CONTROL, 0, udelay) ||
	    !rh_sync(b + RH_ENABLE, 0, udelay) || !rh_sync(b + RH_WAIT, 0, udelay))
		return RH_E_SEND;
	rh_wr(RH_TCS_BASE + RH_IRQ_CLEAR, bit);
	rh_sync(RH_TCS_BASE + RH_IRQ_EN, (rh_rd(RH_TCS_BASE + RH_IRQ_EN) & ~bit) | (irq_en & bit), udelay);
	return 0;
}

#endif
