#include "bld_engine.h"

#include "bld_boot.h"
#include "bld_config.h"
#include "bld_crc32.h"
#include "bld_protocol.h"

#include <string.h>

#define BLD_ENGINE_OK 0
#define BLD_ENGINE_ERR (-1)

#define BLD_DATA_PREFIX_PAYLOAD_SIZE 6u
#define BLD_CRC32_FIELD_SIZE 4u
#define BLD_EOF_FIELD_SIZE 1u
#define BLD_STATUS_PAYLOAD_SIZE 8u
#define BLD_META_PAYLOAD_SIZE 36u
#define BLD_MAX_FRAME_SIZE 1024u

enum {
	BLD_PROTOCOL_COMMON_OVERHEAD =
		4u + BLD_CRC32_FIELD_SIZE + BLD_EOF_FIELD_SIZE,
};

static void bld_engine_reset_session(struct bld_engine *engine)
{
	if (engine == NULL) {
		return;
	}

	memset(&engine->session, 0, sizeof(engine->session));
	engine->target_slot = BLD_SLOT_ID_NONE;
}

static int bld_engine_refresh_boot_control(struct bld_engine *engine)
{
	if (engine == NULL) {
		return BLD_ENGINE_ERR;
	}

	if (bld_meta_read_boot_control(&engine->meta_storage,
				       &engine->boot_ctrl) != 0) {
		memset(&engine->boot_ctrl, 0, sizeof(engine->boot_ctrl));
		engine->boot_ctrl.active_slot = (uint8_t)BLD_SLOT_ID_NONE;
		engine->boot_ctrl.confirmed_slot = (uint8_t)BLD_SLOT_ID_NONE;
		engine->boot_ctrl.pending_slot = (uint8_t)BLD_SLOT_ID_NONE;
		return BLD_ENGINE_ERR;
	}

	return BLD_ENGINE_OK;
}

static uint32_t bld_engine_frame_crc32(const uint8_t *frame,
				       uint32_t crc_input_size)
{
	return bld_crc32_ieee(frame, crc_input_size, BLD_CRC32_INITIAL);
}

static int bld_engine_slot_id_valid(enum bld_slot_id slot)
{
	return (slot == BLD_SLOT_ID_A || slot == BLD_SLOT_ID_B) ?
		       BLD_ENGINE_OK :
		       BLD_ENGINE_ERR;
}

static struct bld_storage *bld_engine_slot_storage(struct bld_engine *engine,
						   enum bld_slot_id slot)
{
	if (engine == NULL || bld_engine_slot_id_valid(slot) != BLD_ENGINE_OK) {
		return NULL;
	}

	return &engine->slot_storage[(uint8_t)slot];
}

static uint32_t bld_engine_slot_base(enum bld_slot_id slot)
{
	return (slot == BLD_SLOT_ID_A) ? BLD_SLOT_A_BASE : BLD_SLOT_B_BASE;
}

static uint32_t bld_engine_slot_size(enum bld_slot_id slot)
{
	return (slot == BLD_SLOT_ID_A) ? BLD_SLOT_A_SIZE : BLD_SLOT_B_SIZE;
}

static int bld_engine_slot_is_bootable(const struct bld_boot_control *ctrl,
				       enum bld_slot_id slot)
{
	uint8_t state;

	if (ctrl == NULL || bld_engine_slot_id_valid(slot) != BLD_ENGINE_OK) {
		return 0;
	}

	state = ctrl->slots[(uint8_t)slot].state;
	return (state == (uint8_t)BLD_SLOT_STATE_CONFIRMED ||
		state == (uint8_t)BLD_SLOT_STATE_VALID ||
		state == (uint8_t)BLD_SLOT_STATE_PENDING);
}

static enum bld_slot_id
bld_engine_choose_target_slot(const struct bld_boot_control *ctrl)
{
	if (ctrl == NULL) {
		return BLD_SLOT_ID_A;
	}

	if (ctrl->active_slot == (uint8_t)BLD_SLOT_ID_A) {
		return BLD_SLOT_ID_B;
	}

	if (ctrl->active_slot == (uint8_t)BLD_SLOT_ID_B) {
		return BLD_SLOT_ID_A;
	}

	if (ctrl->confirmed_slot == (uint8_t)BLD_SLOT_ID_A) {
		return BLD_SLOT_ID_B;
	}

	if (ctrl->confirmed_slot == (uint8_t)BLD_SLOT_ID_B) {
		return BLD_SLOT_ID_A;
	}

	return BLD_SLOT_ID_A;
}

static int bld_engine_send_status(struct bld_engine *engine,
				  enum bld_status status, uint32_t detail)
{
	struct bld_status_frame frame;
	uint32_t crc_input_size;

	if (engine == NULL || engine->transport.send == NULL) {
		return BLD_ENGINE_ERR;
	}

	memset(&frame, 0, sizeof(frame));
	frame.sof = BLD_SOF;
	frame.type = BLD_PKT_STATUS;
	frame.len = BLD_STATUS_PAYLOAD_SIZE;
	frame.status = (uint8_t)status;
	frame.state = (uint8_t)engine->state;
	frame.detail = detail;

	crc_input_size = (uint32_t)(sizeof(frame) - sizeof(frame.crc32) -
				    sizeof(frame.eof));
	frame.crc32 =
		bld_engine_frame_crc32((const uint8_t *)&frame, crc_input_size);
	frame.eof = BLD_EOF;

	return engine->transport.send((uint8_t *)&frame,
				      (uint16_t)sizeof(frame),
				      engine->transport.ctx);
}

static int bld_engine_send_meta(struct bld_engine *engine)
{
	struct bld_meta_frame frame;
	uint32_t crc_input_size;

	if (engine == NULL || engine->transport.send == NULL) {
		return BLD_ENGINE_ERR;
	}

	if (bld_engine_refresh_boot_control(engine) != BLD_ENGINE_OK) {
		memset(&engine->boot_ctrl, 0, sizeof(engine->boot_ctrl));
		engine->boot_ctrl.active_slot = (uint8_t)BLD_SLOT_ID_NONE;
		engine->boot_ctrl.confirmed_slot = (uint8_t)BLD_SLOT_ID_NONE;
		engine->boot_ctrl.pending_slot = (uint8_t)BLD_SLOT_ID_NONE;
	}

	memset(&frame, 0, sizeof(frame));
	frame.sof = BLD_SOF;
	frame.type = BLD_PKT_META;
	frame.len = BLD_META_PAYLOAD_SIZE;

	frame.active_slot = engine->boot_ctrl.active_slot;
	frame.confirmed_slot = engine->boot_ctrl.confirmed_slot;
	frame.pending_slot = engine->boot_ctrl.pending_slot;
	frame.reserved0 = engine->boot_ctrl.reserved0;

	frame.slot_a.version = engine->boot_ctrl.slots[BLD_SLOT_ID_A].version;
	frame.slot_a.size = engine->boot_ctrl.slots[BLD_SLOT_ID_A].size;
	frame.slot_a.crc32 = engine->boot_ctrl.slots[BLD_SLOT_ID_A].crc32;
	frame.slot_a.state = engine->boot_ctrl.slots[BLD_SLOT_ID_A].state;
	frame.slot_a.boot_attempts_left =
		engine->boot_ctrl.slots[BLD_SLOT_ID_A].boot_attempts_left;
	frame.slot_a.reserved = 0u;

	frame.slot_b.version = engine->boot_ctrl.slots[BLD_SLOT_ID_B].version;
	frame.slot_b.size = engine->boot_ctrl.slots[BLD_SLOT_ID_B].size;
	frame.slot_b.crc32 = engine->boot_ctrl.slots[BLD_SLOT_ID_B].crc32;
	frame.slot_b.state = engine->boot_ctrl.slots[BLD_SLOT_ID_B].state;
	frame.slot_b.boot_attempts_left =
		engine->boot_ctrl.slots[BLD_SLOT_ID_B].boot_attempts_left;
	frame.slot_b.reserved = 0u;

	crc_input_size = (uint32_t)(sizeof(frame) - sizeof(frame.crc32) -
				    sizeof(frame.eof));
	frame.crc32 =
		bld_engine_frame_crc32((const uint8_t *)&frame, crc_input_size);
	frame.eof = BLD_EOF;

	return engine->transport.send((uint8_t *)&frame,
				      (uint16_t)sizeof(frame),
				      engine->transport.ctx);
}

static int bld_engine_validate_common_frame(const uint8_t *buf, uint16_t len)
{
	if (buf == NULL || len == 0u) {
		return BLD_ENGINE_ERR;
	}

	if (len < BLD_PROTOCOL_COMMON_OVERHEAD) {
		return BLD_ENGINE_ERR;
	}

	if (buf[0] != BLD_SOF) {
		return BLD_ENGINE_ERR;
	}

	if (buf[len - 1u] != BLD_EOF) {
		return BLD_ENGINE_ERR;
	}

	return BLD_ENGINE_OK;
}

static int bld_engine_validate_crc(const uint8_t *buf, uint16_t len)
{
	uint32_t expected_crc;
	uint32_t actual_crc;
	uint32_t crc_input_size;

	if (buf == NULL || len == 0u ||
	    len < (BLD_CRC32_FIELD_SIZE + BLD_EOF_FIELD_SIZE)) {
		return BLD_ENGINE_ERR;
	}

	memcpy(&expected_crc,
	       &buf[len - (BLD_CRC32_FIELD_SIZE + BLD_EOF_FIELD_SIZE)],
	       sizeof(expected_crc));

	crc_input_size =
		(uint32_t)len - (BLD_CRC32_FIELD_SIZE + BLD_EOF_FIELD_SIZE);
	actual_crc = bld_engine_frame_crc32(buf, crc_input_size);

	return (actual_crc == expected_crc) ? BLD_ENGINE_OK : BLD_ENGINE_ERR;
}

static int bld_engine_verify_slot_image(struct bld_engine *engine,
					enum bld_slot_id slot,
					uint32_t image_size,
					uint32_t image_crc32)
{
	uint8_t chunk[256];
	uint32_t remaining;
	uint32_t offset;
	uint32_t crc;
	uint32_t read_size;
	struct bld_storage *storage;

	if (engine == NULL) {
		return BLD_ENGINE_ERR;
	}

	storage = bld_engine_slot_storage(engine, slot);
	if (storage == NULL || storage->read == NULL) {
		return BLD_ENGINE_ERR;
	}

	if (image_size == 0u || image_crc32 == 0u) {
		return BLD_ENGINE_ERR;
	}

	crc = BLD_CRC32_INITIAL;
	remaining = image_size;
	offset = 0u;

	while (remaining > 0u) {
		read_size = (remaining > sizeof(chunk)) ?
				    (uint32_t)sizeof(chunk) :
				    remaining;

		if (storage->read(storage, offset, chunk, read_size) != 0) {
			return BLD_ENGINE_ERR;
		}

		crc = bld_crc32_ieee(chunk, read_size, crc);
		remaining -= read_size;
		offset += read_size;
	}

	return (crc == image_crc32) ? BLD_ENGINE_OK : BLD_ENGINE_ERR;
}

static int bld_engine_handle_cmd(struct bld_engine *engine,
				 const struct bld_cmd_frame *frame)
{
	if (engine == NULL || frame == NULL) {
		return BLD_ENGINE_ERR;
	}

	if (frame->type != BLD_PKT_CMD) {
		return bld_engine_send_status(engine, BLD_ST_BAD_FRAME, 0u);
	}

	if (frame->cmd == BLD_CMD_ABORT) {
		bld_engine_reset_session(engine);
		engine->state = BLD_STATE_IDLE;
		return bld_engine_send_status(engine, BLD_ST_OK, 0u);
	}

	switch (engine->state) {
	case BLD_STATE_IDLE:
		if (frame->cmd == BLD_CMD_META) {
			(void)bld_engine_refresh_boot_control(engine);
			return bld_engine_send_meta(engine);
		}

		if (frame->cmd == BLD_CMD_START) {
			(void)bld_engine_refresh_boot_control(engine);
			bld_engine_reset_session(engine);
			engine->target_slot = bld_engine_choose_target_slot(
				&engine->boot_ctrl);
			engine->state = BLD_STATE_WAIT_HEADER;
			return bld_engine_send_status(
				engine, BLD_ST_OK,
				(uint32_t)engine->target_slot);
		}

		if (frame->cmd == BLD_CMD_QUERY) {
			return bld_engine_send_status(engine, BLD_ST_OK, 0u);
		}

		if (frame->cmd == BLD_CMD_BOOT) {
			if (bld_engine_boot_decide_and_jump(engine) != 0) {
				return bld_engine_send_status(
					engine, BLD_ST_BOOT_ERR, 0u);
			}
			return BLD_ENGINE_OK;
		}
		break;

	case BLD_STATE_WAIT_END:
		if (frame->cmd == BLD_CMD_END) {
			if (engine->session.received_size !=
			    engine->session.image_size) {
				engine->state = BLD_STATE_ERROR;
				return bld_engine_send_status(
					engine, BLD_ST_BAD_FRAME,
					engine->session.received_size);
			}

			if (bld_engine_verify_slot_image(
				    engine, engine->target_slot,
				    engine->session.image_size,
				    engine->session.image_crc32) != 0) {
				engine->state = BLD_STATE_ERROR;
				return bld_engine_send_status(
					engine, BLD_ST_BAD_CRC,
					engine->session.image_crc32);
			}

			if (bld_meta_set_pending(&engine->meta_storage,
						 engine->target_slot,
						 engine->session.image_version,
						 engine->session.image_size,
						 engine->session.image_crc32,
						 BLD_MAX_BOOT_ATTEMPTS) != 0) {
				engine->state = BLD_STATE_ERROR;
				return bld_engine_send_status(
					engine, BLD_ST_FLASH_ERR, 0u);
			}

			(void)bld_engine_refresh_boot_control(engine);
			bld_engine_reset_session(engine);
			engine->state = BLD_STATE_IDLE;
			return bld_engine_send_status(engine, BLD_ST_OK, 0u);
		}
		break;

	default:
		break;
	}

	return bld_engine_send_status(engine, BLD_ST_BAD_STATE, frame->cmd);
}

static int bld_engine_handle_header(struct bld_engine *engine,
				    const struct bld_header_frame *frame)
{
	struct bld_storage *storage;
	uint32_t slot_size;

	if (engine == NULL || frame == NULL) {
		return BLD_ENGINE_ERR;
	}

	if (engine->state != BLD_STATE_WAIT_HEADER) {
		return bld_engine_send_status(engine, BLD_ST_BAD_STATE,
					      engine->state);
	}

	if (frame->type != BLD_PKT_HEADER) {
		return bld_engine_send_status(engine, BLD_ST_BAD_FRAME, 0u);
	}

	if (bld_engine_slot_id_valid(engine->target_slot) != BLD_ENGINE_OK) {
		engine->state = BLD_STATE_ERROR;
		return bld_engine_send_status(engine, BLD_ST_FLASH_ERR, 0u);
	}

	slot_size = bld_engine_slot_size(engine->target_slot);
	if (frame->image_size == 0u || frame->image_size > slot_size) {
		engine->state = BLD_STATE_IDLE;
		bld_engine_reset_session(engine);
		return bld_engine_send_status(engine, BLD_ST_TOO_LARGE,
					      frame->image_size);
	}

	storage = bld_engine_slot_storage(engine, engine->target_slot);
	if (storage == NULL || storage->erase == NULL) {
		engine->state = BLD_STATE_ERROR;
		return bld_engine_send_status(engine, BLD_ST_FLASH_ERR, 0u);
	}

	if (storage->erase(storage, 0u, frame->image_size) != 0) {
		engine->state = BLD_STATE_ERROR;
		return bld_engine_send_status(engine, BLD_ST_FLASH_ERR, 0u);
	}

	engine->session.expected_seq = 0u;
	engine->session.received_size = 0u;
	engine->session.image_size = frame->image_size;
	engine->session.image_crc32 = frame->image_crc32;
	engine->session.image_version = frame->version;
	engine->state = BLD_STATE_RECV_DATA;

	return bld_engine_send_status(engine, BLD_ST_OK, 0u);
}

static int bld_engine_handle_data(struct bld_engine *engine, const uint8_t *buf,
				  uint16_t len)
{
	const struct bld_data_prefix *frame;
	uint16_t payload_len;
	uint16_t chunk_len;
	uint32_t expected_total_len;
	struct bld_storage *storage;

	if (engine == NULL || buf == NULL) {
		return BLD_ENGINE_ERR;
	}

	if (engine->state != BLD_STATE_RECV_DATA) {
		return bld_engine_send_status(engine, BLD_ST_BAD_STATE,
					      engine->state);
	}

	frame = (const struct bld_data_prefix *)buf;
	if (frame->type != BLD_PKT_DATA) {
		return bld_engine_send_status(engine, BLD_ST_BAD_FRAME, 0u);
	}

	payload_len = frame->len;
	if (payload_len < BLD_DATA_PREFIX_PAYLOAD_SIZE) {
		return bld_engine_send_status(engine, BLD_ST_BAD_FRAME,
					      payload_len);
	}

	chunk_len = frame->chunk_len;
	if ((uint32_t)chunk_len + BLD_DATA_PREFIX_PAYLOAD_SIZE != payload_len) {
		return bld_engine_send_status(engine, BLD_ST_BAD_FRAME,
					      chunk_len);
	}

	expected_total_len =
		(uint32_t)payload_len + BLD_PROTOCOL_COMMON_OVERHEAD;
	if (len != expected_total_len) {
		return bld_engine_send_status(engine, BLD_ST_BAD_FRAME, len);
	}

	if (frame->seq != engine->session.expected_seq) {
		return bld_engine_send_status(engine, BLD_ST_SEQ_ERR,
					      engine->session.expected_seq);
	}

	if (engine->session.received_size + chunk_len >
	    engine->session.image_size) {
		engine->state = BLD_STATE_ERROR;
		return bld_engine_send_status(engine, BLD_ST_BAD_FRAME,
					      engine->session.received_size +
						      chunk_len);
	}

	storage = bld_engine_slot_storage(engine, engine->target_slot);
	if (storage == NULL || storage->write == NULL) {
		engine->state = BLD_STATE_ERROR;
		return bld_engine_send_status(engine, BLD_ST_FLASH_ERR, 0u);
	}

	if (storage->write(storage, engine->session.received_size, frame->data,
			   chunk_len) != 0) {
		engine->state = BLD_STATE_ERROR;
		return bld_engine_send_status(engine, BLD_ST_FLASH_ERR,
					      engine->session.received_size);
	}

	engine->session.received_size += chunk_len;
	engine->session.expected_seq += 1u;

	if (engine->session.received_size >= engine->session.image_size) {
		engine->state = BLD_STATE_WAIT_END;
	}

	return bld_engine_send_status(engine, BLD_ST_OK, 0u);
}

int bld_engine_init(struct bld_engine *engine,
		    const struct bld_transport *transport,
		    const struct bld_storage *slot_a_storage,
		    const struct bld_storage *slot_b_storage,
		    const struct bld_storage *meta_storage)
{
	if (engine == NULL || transport == NULL || slot_a_storage == NULL ||
	    slot_b_storage == NULL || meta_storage == NULL) {
		return BLD_ENGINE_ERR;
	}

	memset(engine, 0, sizeof(*engine));
	engine->state = BLD_STATE_IDLE;
	engine->transport = *transport;
	engine->slot_storage[BLD_SLOT_ID_A] = *slot_a_storage;
	engine->slot_storage[BLD_SLOT_ID_B] = *slot_b_storage;
	engine->meta_storage = *meta_storage;
	engine->target_slot = BLD_SLOT_ID_NONE;

	(void)bld_engine_refresh_boot_control(engine);
	return BLD_ENGINE_OK;
}

int bld_engine_boot_decide_and_jump(struct bld_engine *engine)
{
	enum bld_slot_id slot;
	uint8_t attempts_left;

	if (engine == NULL) {
		return BLD_ENGINE_ERR;
	}

	if (bld_engine_refresh_boot_control(engine) != 0) {
		return BLD_ENGINE_ERR;
	}

	if (engine->boot_ctrl.pending_slot != (uint8_t)BLD_SLOT_ID_NONE) {
		slot = (enum bld_slot_id)engine->boot_ctrl.pending_slot;

		if (!bld_engine_slot_is_bootable(&engine->boot_ctrl, slot)) {
			(void)bld_meta_mark_slot_bad(&engine->meta_storage,
						     slot);
			(void)bld_engine_refresh_boot_control(engine);
		} else if (engine->boot_ctrl.slots[(uint8_t)slot]
				   .boot_attempts_left == 0u) {
			(void)bld_meta_mark_slot_bad(&engine->meta_storage,
						     slot);
			(void)bld_engine_refresh_boot_control(engine);
		} else if (bld_engine_verify_slot_image(
				   engine, slot,
				   engine->boot_ctrl.slots[(uint8_t)slot].size,
				   engine->boot_ctrl.slots[(uint8_t)slot]
					   .crc32) == 0) {
			if (bld_meta_decrement_pending_attempts(
				    &engine->meta_storage, &attempts_left) ==
			    0) {
				(void)bld_engine_send_status(engine, BLD_ST_OK,
							     (uint32_t)slot);
				bld_jump_to_image(bld_engine_slot_base(slot));
				return BLD_ENGINE_OK;
			}

			(void)bld_meta_mark_slot_bad(&engine->meta_storage,
						     slot);
			(void)bld_engine_refresh_boot_control(engine);
		} else {
			(void)bld_meta_mark_slot_bad(&engine->meta_storage,
						     slot);
			(void)bld_engine_refresh_boot_control(engine);
		}
	}

	if (engine->boot_ctrl.confirmed_slot != (uint8_t)BLD_SLOT_ID_NONE) {
		slot = (enum bld_slot_id)engine->boot_ctrl.confirmed_slot;

		if (bld_engine_verify_slot_image(
			    engine, slot,
			    engine->boot_ctrl.slots[(uint8_t)slot].size,
			    engine->boot_ctrl.slots[(uint8_t)slot].crc32) ==
		    0) {
			(void)bld_engine_send_status(engine, BLD_ST_OK,
						     (uint32_t)slot);
			bld_jump_to_image(bld_engine_slot_base(slot));
			return BLD_ENGINE_OK;
		}

		(void)bld_meta_mark_slot_bad(&engine->meta_storage, slot);
		(void)bld_engine_refresh_boot_control(engine);
	}

	(void)bld_engine_send_status(engine, BLD_ST_BOOT_ERR, 0u);
	return BLD_ENGINE_ERR;
}

void bld_engine_poll(struct bld_engine *engine, uint32_t frame_timeout_ms)
{
	uint8_t frame_buf[BLD_MAX_FRAME_SIZE];
	int frame_len;
	uint8_t frame_type;

	if (engine == NULL || engine->transport.parse == NULL) {
		return;
	}

	frame_len = engine->transport.parse(frame_buf,
					    (uint16_t)sizeof(frame_buf),
					    frame_timeout_ms,
					    engine->transport.ctx);

	/* Parse error */
	if (frame_len < 0) {
		return;
	}

	/* Timeout/no frame */
	if (frame_len == 0) {
		return;
	}

	if (bld_engine_validate_common_frame(frame_buf, (uint16_t)frame_len) !=
	    0) {
		(void)bld_engine_send_status(engine, BLD_ST_BAD_FRAME,
					     (uint32_t)frame_len);
		return;
	}

	if (bld_engine_validate_crc(frame_buf, (uint16_t)frame_len) != 0) {
		(void)bld_engine_send_status(engine, BLD_ST_BAD_CRC,
					     (uint32_t)frame_len);
		return;
	}

	frame_type = frame_buf[1];

	if (frame_type == BLD_PKT_CMD) {
		if ((uint16_t)frame_len != sizeof(struct bld_cmd_frame)) {
			(void)bld_engine_send_status(engine, BLD_ST_BAD_FRAME,
						     (uint32_t)frame_len);
			return;
		}

		(void)bld_engine_handle_cmd(
			engine, (const struct bld_cmd_frame *)frame_buf);
		return;
	}

	if (frame_type == BLD_PKT_HEADER) {
		if ((uint16_t)frame_len != sizeof(struct bld_header_frame)) {
			(void)bld_engine_send_status(engine, BLD_ST_BAD_FRAME,
						     (uint32_t)frame_len);
			return;
		}

		(void)bld_engine_handle_header(
			engine, (const struct bld_header_frame *)frame_buf);
		return;
	}

	if (frame_type == BLD_PKT_DATA) {
		(void)bld_engine_handle_data(engine, frame_buf,
					     (uint16_t)frame_len);
		return;
	}

	(void)bld_engine_send_status(engine, BLD_ST_BAD_FRAME,
				     (uint32_t)frame_type);
}