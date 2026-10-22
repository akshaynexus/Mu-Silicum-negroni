#ifndef NEGRONI_SYNA_TCM_H
#define NEGRONI_SYNA_TCM_H

#include <stdint.h>

#define TCM_MARKER			0xA5
#define TCM_PADDING			0x5A
#define TCM_STATUS_IDLE			0x00
#define TCM_STATUS_OK			0x01
#define TCM_STATUS_BUSY			0x02
#define TCM_STATUS_CONTINUED_READ	0x03
#define TCM_REPORT_IDENTIFY		0x10
#define TCM_REPORT_TOUCH		0x11
#define TCM_CMD_GET_TOUCH_REPORT_CONFIG	0x25

#define TCM_MAX_OBJECTS			10
#define TCM_MAX_PAYLOAD			512
#define TCM_MAX_CONFIG			256

enum {
	TCM_TOUCH_END = 0,
	TCM_TOUCH_FOREACH_ACTIVE_OBJECT,
	TCM_TOUCH_FOREACH_OBJECT,
	TCM_TOUCH_FOREACH_END,
	TCM_TOUCH_PAD_TO_NEXT_BYTE,
	TCM_TOUCH_TIMESTAMP,
	TCM_TOUCH_OBJECT_N_INDEX,
	TCM_TOUCH_OBJECT_N_CLASSIFICATION,
	TCM_TOUCH_OBJECT_N_X_POSITION,
	TCM_TOUCH_OBJECT_N_Y_POSITION,
	TCM_TOUCH_OBJECT_N_Z,
	TCM_TOUCH_NUM_OF_ACTIVE_OBJECTS = 24,
};

#define TCM_PANEL_MAX_X			5760
#define TCM_PANEL_MAX_Y			12864

struct tcm_io {
	void *ctx;
	int (*read)(void *ctx, uint8_t *buf, uint32_t len);
	int (*write)(void *ctx, const uint8_t *buf, uint32_t len);
};

struct tcm_object {
	uint8_t status;
	uint32_t x, y, z;
};

struct tcm {
	struct tcm_io io;
	uint8_t config[TCM_MAX_CONFIG];
	uint32_t config_len;
	uint8_t payload[TCM_MAX_PAYLOAD];
	uint32_t payload_len;
	uint8_t code;
	uint8_t header[4];
	int transport_error;
	struct tcm_object obj[TCM_MAX_OBJECTS];
};

static inline int tcm_read_message(struct tcm *t)
{
	uint8_t *hdr = t->header;
	uint8_t buf[TCM_MAX_PAYLOAD + 3];
	uint32_t len;

	t->code = 0;
	t->payload_len = 0;
	for (uint32_t i = 0; i < 4; i++)
		hdr[i] = 0;
	t->transport_error = t->io.read(t->io.ctx, hdr, 4);
	if (t->transport_error)
		return -1;
	if (hdr[0] != TCM_MARKER)
		return 1;
	t->code = hdr[1];

	if (t->code == TCM_STATUS_IDLE || t->code == TCM_STATUS_BUSY)
		return 1;
	if (t->code == TCM_STATUS_CONTINUED_READ)
		return -3;
	len = hdr[2] | ((uint32_t)hdr[3] << 8);
	t->payload_len = 0;
	if (!len)
		return 0;
	if (len > TCM_MAX_PAYLOAD)
		return -2;
	t->transport_error = t->io.read(t->io.ctx, buf, len + 3);
	if (t->transport_error)
		return -1;
	if (buf[0] != TCM_MARKER || buf[1] != TCM_STATUS_CONTINUED_READ || buf[len + 2] != TCM_PADDING)
		return -3;
	for (uint32_t i = 0; i < len; i++)
		t->payload[i] = buf[2 + i];
	t->payload_len = len;
	return 0;
}

static inline int tcm_write_command(struct tcm *t, uint8_t cmd, const uint8_t *payload, uint32_t len)
{
	uint8_t buf[3 + 32];

	if (len > 32)
		return -2;
	buf[0] = cmd;
	buf[1] = (uint8_t)len;
	buf[2] = (uint8_t)(len >> 8);
	for (uint32_t i = 0; i < len; i++)
		buf[3 + i] = payload[i];
	t->transport_error = t->io.write(t->io.ctx, buf, 3 + len);
	return t->transport_error ? -1 : 0;
}

static inline int tcm_command(struct tcm *t, uint8_t cmd, const uint8_t *payload, uint32_t len,
			      void (*wait)(void), uint32_t tries)
{
	if (tcm_write_command(t, cmd, payload, len))
		return -1;
	while (tries--) {
		int rc = tcm_read_message(t);

		if (rc < 0)
			return rc;
		if (rc == 0 && t->code < TCM_REPORT_IDENTIFY)
			return t->code == TCM_STATUS_OK ? 0 : -4;
		wait();
	}
	return -5;
}

static inline int tcm_get_report_config(struct tcm *t, void (*wait)(void))
{
	int rc = tcm_command(t, TCM_CMD_GET_TOUCH_REPORT_CONFIG, 0, 0, wait, 200);

	if (rc)
		return rc;
	if (t->payload_len > TCM_MAX_CONFIG)
		return -2;
	for (uint32_t i = 0; i < t->payload_len; i++)
		t->config[i] = t->payload[i];
	t->config_len = t->payload_len;
	return 0;
}

static inline uint32_t tcm_bits(const struct tcm *t, uint32_t offset, uint32_t bits)
{
	uint32_t out = 0, done = 0;

	if (!bits || bits > 32 || offset + bits > t->payload_len * 8)
		return 0;
	while (done < bits) {
		uint32_t byte = offset / 8, shift = offset % 8;
		uint32_t take = 8 - shift < bits - done ? 8 - shift : bits - done;
		uint32_t v = ((uint32_t)t->payload[byte] >> shift) & (0xffu >> (8 - take));

		out |= v << done;
		done += take;
		offset += take;
	}
	return out;
}

static inline int tcm_parse_touch(struct tcm *t)
{
	uint32_t idx = 0, offset = 0, obj = 0, next = 0, objects = 0, active = 0;
	int active_only = 0, have_active = 0;

	for (int i = 0; i < TCM_MAX_OBJECTS; i++)
		t->obj[i].status = 0;
	while (idx < t->config_len) {
		uint8_t code = t->config[idx++];
		uint32_t bits, v;

		switch (code) {
		case TCM_TOUCH_END:
			return 0;
		case TCM_TOUCH_FOREACH_ACTIVE_OBJECT:
		case TCM_TOUCH_FOREACH_OBJECT:
			obj = 0;
			next = idx;
			active_only = code == TCM_TOUCH_FOREACH_ACTIVE_OBJECT;
			break;
		case TCM_TOUCH_FOREACH_END:
			if (active_only) {
				if (have_active) {
					if (++objects < active)
						idx = next;
				} else if (offset < t->payload_len * 8) {
					idx = next;
				}
			} else if (++obj < TCM_MAX_OBJECTS) {
				idx = next;
			}
			break;
		case TCM_TOUCH_PAD_TO_NEXT_BYTE:
			offset = (offset + 7) & ~7u;
			break;
		default:
			if (idx >= t->config_len)
				return -1;
			bits = t->config[idx++];
			v = tcm_bits(t, offset, bits);
			offset += bits;
			switch (code) {
			case TCM_TOUCH_OBJECT_N_INDEX:
				if (v >= TCM_MAX_OBJECTS)
					return -1;
				obj = v;
				break;
			case TCM_TOUCH_OBJECT_N_CLASSIFICATION:
				t->obj[obj].status = (uint8_t)v;
				break;
			case TCM_TOUCH_OBJECT_N_X_POSITION:
				t->obj[obj].x = v;
				break;
			case TCM_TOUCH_OBJECT_N_Y_POSITION:
				t->obj[obj].y = v;
				break;
			case TCM_TOUCH_OBJECT_N_Z:
				t->obj[obj].z = v;
				break;
			case TCM_TOUCH_NUM_OF_ACTIVE_OBJECTS:
				active = v;
				have_active = 1;
				if (!active)
					return 0;
				break;
			default:
				break;
			}
		}
	}
	return 0;
}

#endif
