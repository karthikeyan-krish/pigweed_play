#include "bld_engine.h"

#include "bld_boot.h"
#include "bld_config.h"
#include "bld_crc32.h"
#include "bld_protocol.h"

#include <string.h>

#define BLD_ENGINE_OK 0
#define BLD_ENGINE_ERR -1

#define BLD_DATA_PREFIX_PAYLOAD_SIZE 6u
#define BLD_CRC32_FIELD_SIZE 4u
#define BLD_EOF_FIELD_SIZE 1u
#define BLD_STATUS_PAYLOAD_SIZE 8u
#define BLD_META_PAYLOAD_SIZE 13u
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
}

static int bld_engine_refresh_image_info(struct bld_engine *engine)
{
	if (engine == NULL) {
		return BLD_ENGINE_ERR;
	}

	if (bld_meta_read_info(&engine->meta_storage, &engine->image_info) !=
	    0) {
		memset(&engine->image_info, 0, sizeof(engine->image_info));
		engine->image_info.state = BLD_IMAGE_STATE_EMPTY;
		return BLD_ENGINE_ERR;
	}
	return BLD_ENGINE_OK;
}

static uint32_t bld_engine_frame_crc32(const uint8_t *frame,
				       uint32_t crc_input_size)
{
	return bld_crc32_ieee(frame, crc_input_size, BLD_CRC32_INITIAL);
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

	memset(&frame, 0, sizeof(frame));
	frame.sof = BLD_SOF;
	frame.type = BLD_PKT_META;
	frame.len = BLD_META_PAYLOAD_SIZE;
	frame.image_version = engine->image_info.version;
	frame.image_size = engine->image_info.size;
	frame.image_crc32 = engine->image_info.crc32;
	frame.state = engine->image_info.state;

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
	if (buf == NULL || len == 0) {
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

	if (buf == NULL || len == 0 ||
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
					uint32_t image_size,
					uint32_t image_crc32)
{
	uint8_t chunk[256];
	uint32_t remaining;
	uint32_t offset;
	uint32_t crc;
	uint32_t read_size;

	if (engine == NULL || engine->slot_storage.read == NULL) {
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

		if (engine->slot_storage.read(&engine->slot_storage, offset,
					      chunk, read_size) != 0) {
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
			(void)bld_engine_refresh_image_info(engine);
			return bld_engine_send_meta(engine);
		}

		if (frame->cmd == BLD_CMD_START) {
			bld_engine_reset_session(engine);
			engine->state = BLD_STATE_WAIT_HEADER;
			return bld_engine_send_status(engine, BLD_ST_OK, 0u);
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
				    engine, engine->session.image_size,
				    engine->session.image_crc32) != 0) {
				engine->state = BLD_STATE_ERROR;
				return bld_engine_send_status(
					engine, BLD_ST_BAD_CRC,
					engine->session.image_crc32);
			}

			if (bld_meta_write_info(&engine->meta_storage,
						engine->session.image_version,
						engine->session.image_size,
						engine->session.image_crc32) !=
			    0) {
				engine->state = BLD_STATE_ERROR;
				return bld_engine_send_status(
					engine, BLD_ST_FLASH_ERR, 0u);
			}

			engine->image_info.version =
				engine->session.image_version;
			engine->image_info.size = engine->session.image_size;
			engine->image_info.crc32 = engine->session.image_crc32;
			engine->image_info.state = BLD_IMAGE_STATE_READY;

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

	if (frame->image_size == 0u || frame->image_size > BLD_SLOT_SIZE) {
		engine->state = BLD_STATE_IDLE;
		bld_engine_reset_session(engine);
		return bld_engine_send_status(engine, BLD_ST_TOO_LARGE,
					      frame->image_size);
	}

	if (engine->slot_storage.erase == NULL) {
		engine->state = BLD_STATE_ERROR;
		return bld_engine_send_status(engine, BLD_ST_FLASH_ERR, 0u);
	}

	if (engine->slot_storage.erase(&engine->slot_storage, 0u,
				       frame->image_size) != 0) {
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

	if (engine->slot_storage.write == NULL) {
		engine->state = BLD_STATE_ERROR;
		return bld_engine_send_status(engine, BLD_ST_FLASH_ERR, 0u);
	}

	if (engine->slot_storage.write(&engine->slot_storage,
				       engine->session.received_size,
				       frame->data, chunk_len) != 0) {
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
		    const struct bld_storage *slot_storage,
		    const struct bld_storage *meta_storage)
{
	if (engine == NULL || transport == NULL || slot_storage == NULL ||
	    meta_storage == NULL) {
		return BLD_ENGINE_ERR;
	}

	memset(engine, 0, sizeof(*engine));
	engine->state = BLD_STATE_IDLE;
	engine->transport = *transport;
	engine->slot_storage = *slot_storage;
	engine->meta_storage = *meta_storage;

	(void)bld_engine_refresh_image_info(engine);
	return BLD_ENGINE_OK;
}

int bld_engine_boot_decide_and_jump(struct bld_engine *engine)
{
	if (engine == NULL) {
		return BLD_ENGINE_ERR;
	}

	if (bld_engine_refresh_image_info(engine) != 0) {
		return BLD_ENGINE_ERR;
	}

	if (engine->image_info.state != BLD_IMAGE_STATE_READY) {
		return BLD_ENGINE_ERR;
	}

	if (bld_engine_verify_slot_image(engine, engine->image_info.size,
					 engine->image_info.crc32) != 0) {
		(void)bld_meta_set_state(&engine->meta_storage,
					 BLD_IMAGE_STATE_CORRUPTED);
		engine->image_info.state = BLD_IMAGE_STATE_CORRUPTED;
		return BLD_ENGINE_ERR;
	}

	(void)bld_engine_send_status(engine, BLD_ST_OK, 0u);

	bld_jump_to_image(BLD_SLOT_BASE);
	return BLD_ENGINE_OK;
}

void bld_engine_poll(struct bld_engine *engine, uint32_t frame_timeout_ms)
{
	uint8_t frame_buf[BLD_MAX_FRAME_SIZE];
	uint16_t frame_len;
	uint8_t frame_type;

	if (engine == NULL || engine->transport.parse == NULL) {
		return;
	}

	frame_len = engine->transport.parse(frame_buf,
					    (uint16_t)sizeof(frame_buf),
					    frame_timeout_ms,
					    engine->transport.ctx);

	if (frame_len == 0u) {
		return;
	}

	if (bld_engine_validate_common_frame(frame_buf, frame_len) != 0) {
		(void)bld_engine_send_status(engine, BLD_ST_BAD_FRAME,
					     frame_len);
		return;
	}

	if (bld_engine_validate_crc(frame_buf, frame_len) != 0) {
		(void)bld_engine_send_status(engine, BLD_ST_BAD_CRC, frame_len);
		return;
	}

	frame_type = frame_buf[1];

	if (frame_type == BLD_PKT_CMD) {
		if (frame_len != sizeof(struct bld_cmd_frame)) {
			(void)bld_engine_send_status(engine, BLD_ST_BAD_FRAME,
						     frame_len);
			return;
		}

		(void)bld_engine_handle_cmd(
			engine, (const struct bld_cmd_frame *)frame_buf);
		return;
	}

	if (frame_type == BLD_PKT_HEADER) {
		if (frame_len != sizeof(struct bld_header_frame)) {
			(void)bld_engine_send_status(engine, BLD_ST_BAD_FRAME,
						     frame_len);
			return;
		}

		(void)bld_engine_handle_header(
			engine, (const struct bld_header_frame *)frame_buf);
		return;
	}

	if (frame_type == BLD_PKT_DATA) {
		(void)bld_engine_handle_data(engine, frame_buf, frame_len);
		return;
	}

	(void)bld_engine_send_status(engine, BLD_ST_BAD_FRAME, frame_type);
}