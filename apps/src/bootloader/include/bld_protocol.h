#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Bootloader serial protocol framing
 *
 * Frame format:
 *
 *   SOF | TYPE | LEN | PAYLOAD | CRC32 | EOF
 *
 * The LEN field defines the number of bytes between the LEN field and the
 * CRC32 field. LEN does not include:
 *
 *   - SOF
 *   - TYPE
 *   - LEN itself
 *   - CRC32
 *   - EOF
 *
 * Each frame type defines its own payload layout.
 */

#define BLD_SOF (0xA5u)
#define BLD_EOF (0x5Au)

/*
 * Bootloader packet types
 *
 * Packet types define the structure of the payload contained in a frame.
 * The bootloader uses distinct packet types for command control, firmware
 * transfer, status reporting, and metadata exchange.
 */
enum bld_pkt_type {
	BLD_PKT_CMD = 0x01,
	BLD_PKT_HEADER = 0x02,
	BLD_PKT_DATA = 0x03,
	BLD_PKT_STATUS = 0x04,
	BLD_PKT_META = 0x05,
};

/*
 * Bootloader commands
 *
 * Commands control firmware update session flow and boot behavior.
 * These commands are sent inside command frames.
 *
 * The available commands are:
 *  - START: begin firmware transfer session
 *  - ABORT: cancel current transfer
 *  - END: finalize transfer and validate image
 *  - QUERY: request bootloader state
 *  - META: transfer metadata information
 *  - BOOT: jump to application image
 */
enum bld_cmd {
	BLD_CMD_START = 0x10,
	BLD_CMD_ABORT = 0x11,
	BLD_CMD_END = 0x12,
	BLD_CMD_QUERY = 0x13,
	BLD_CMD_META = 0x14,
	BLD_CMD_BOOT = 0x15,
};

/*
 * Bootloader status codes
 *
 * Status codes are returned in status frames to indicate the result of an
 * operation or the current state of the bootloader.
 */
enum bld_status {
	BLD_ST_OK = 0,
	BLD_ST_ERR = 1,
	BLD_ST_BAD_FRAME = 2,
	BLD_ST_BAD_CRC = 3,
	BLD_ST_TOO_LARGE = 4,
	BLD_ST_BAD_STATE = 5,
	BLD_ST_FLASH_ERR = 6,
	BLD_ST_SEQ_ERR = 7,
	BLD_ST_BOOT_ERR = 8,
};

/*
 * Command frame
 *
 * Used to send control commands to the bootloader.
 * Payload consists of command identifier and reserved bytes.
 */
struct __attribute__((packed)) bld_cmd_frame {
	uint8_t sof;
	uint8_t type;
	uint16_t len;
	uint8_t cmd;
	uint8_t reserved[3];
	uint32_t crc32;
	uint8_t eof;
};

/*
 * Firmware header frame
 *
 * Describes the firmware image that will be transferred.
 * Contains image size, CRC, and version information.
 */
struct __attribute__((packed)) bld_header_frame {
	uint8_t sof;
	uint8_t type;
	uint16_t len;
	uint32_t image_size;
	uint32_t image_crc32;
	uint32_t version;
	uint32_t crc32;
	uint8_t eof;
};

/*
 * Firmware data frame prefix
 *
 * Used to transfer firmware payload in chunks.
 *
 * Frame layout on wire:
 *
 *   prefix + data[chunk_len] + crc32 + eof
 *
 * The flexible array member allows variable payload size.
 */
struct __attribute__((packed)) bld_data_prefix {
	uint8_t sof;
	uint8_t type;
	uint16_t len;
	uint32_t seq;
	uint16_t chunk_len;
	uint8_t data[];
};

/*
 * Status frame
 *
 * Returned by the bootloader in response to commands or data frames.
 * Contains operation status and implementation-specific detail.
 */
struct __attribute__((packed)) bld_status_frame {
	uint8_t sof;
	uint8_t type;
	uint16_t len;
	uint8_t status;
	uint8_t state;
	uint16_t reserved;
	uint32_t detail;
	uint32_t crc32;
	uint8_t eof;
};

/*
 * Metadata frame
 *
 * Transfers full boot-control metadata for A/B slot management.
 *
 * This allows the host to inspect both slots and decide which image/linker
 * layout should be transmitted.
 */
struct __attribute__((packed)) bld_meta_slot_wire {
	uint32_t version;
	uint32_t size;
	uint32_t crc32;
	uint8_t state;
	uint8_t boot_attempts_left;
	uint16_t reserved;
};

struct __attribute__((packed)) bld_meta_frame {
	uint8_t sof;
	uint8_t type;
	uint16_t len;

	uint8_t active_slot;
	uint8_t confirmed_slot;
	uint8_t pending_slot;
	uint8_t reserved0;

	struct bld_meta_slot_wire slot_a;
	struct bld_meta_slot_wire slot_b;

	uint32_t crc32;
	uint8_t eof;
};

#ifdef __cplusplus
}
#endif