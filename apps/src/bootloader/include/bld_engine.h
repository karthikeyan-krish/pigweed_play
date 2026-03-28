#pragma once
#include <stdint.h>

#include "bld_meta.h"
#include "bld_storage.h"
#include "bld_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Bootloader engine states.
 *
 * These states describe the current firmware update session state.
 */
enum bld_state {
	BLD_STATE_IDLE = 0,
	BLD_STATE_WAIT_HEADER = 1,
	BLD_STATE_RECV_DATA = 2,
	BLD_STATE_WAIT_END = 3,
	BLD_STATE_ERROR = 4,
};

/*
 * Runtime transfer session state.
 *
 * This state exists only while a firmware transfer is in progress.
 */
struct bld_session {
	uint32_t expected_seq;
	uint32_t received_size;
	uint32_t image_size;
	uint32_t image_crc32;
	uint32_t image_version;
};

/*
 * Bootloader engine context.
 *
 * The engine owns the protocol state machine and uses the transport,
 * firmware-slot storage, and metadata storage provided by the caller.
 */
struct bld_engine {
	enum bld_state state;
	struct bld_transport transport;
	struct bld_storage slot_storage;
	struct bld_storage meta_storage;
	struct bld_image_info image_info;
	struct bld_session session;
};

/*
 * Initializes the bootloader engine.
 *
 * The transport, slot storage, and metadata storage objects are copied into
 * the engine. The current image metadata is loaded from persistent storage.
 */
int bld_engine_init(struct bld_engine *engine,
		    const struct bld_transport *transport,
		    const struct bld_storage *slot_storage,
		    const struct bld_storage *meta_storage);

/*
 * Processes one incoming transport frame.
 *
 * This function polls the transport, validates the received frame, and
 * advances the bootloader protocol state machine.
 */
void bld_engine_poll(struct bld_engine *engine, uint32_t frame_timeout_ms);

/*
 * Validates the stored image and jumps to it.
 *
 * Returns 0 on success. On a successful jump this function does not return.
 * Returns a negative value if no bootable image is available.
 */
int bld_engine_boot_decide_and_jump(struct bld_engine *engine);

#ifdef __cplusplus
}
#endif
