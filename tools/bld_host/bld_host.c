#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#define BLD_HOST_DEFAULT_BAUD 115200
#define BLD_HOST_DEFAULT_CHUNK 128u
#define BLD_HOST_DEFAULT_TIMEOUT_MS 5000
#define BLD_HOST_DEFAULT_VERSION 0x00000001u
#define BLD_HOST_POLL_STEP_MS 50
#define BLD_HOST_DROP_BUF_SIZE 256u

#define BLD_FRAME_PREFIX_SIZE 4u
#define BLD_FRAME_CRC32_SIZE 4u
#define BLD_FRAME_EOF_SIZE 1u
#define BLD_FRAME_STATUS_PAYLOAD_SIZE 8u
#define BLD_FRAME_META_PAYLOAD_SIZE 13u
#define BLD_CMD_RESERVED_SIZE 3u
#define BLD_DATA_PREFIX_PAYLOAD_SIZE 6u

#define BLD_CRC32_INITIAL 0xFFFFFFFFu
#define BLD_CRC32_POLY 0xEDB88320u

#define BLD_SOF 0xA5u
#define BLD_EOF 0x5Au

enum bld_pkt_type {
  BLD_PKT_CMD = 0x01,
  BLD_PKT_HEADER = 0x02,
  BLD_PKT_DATA = 0x03,
  BLD_PKT_STATUS = 0x04,
  BLD_PKT_META = 0x05,
};

enum bld_cmd {
  BLD_CMD_START = 0x10,
  BLD_CMD_ABORT = 0x11,
  BLD_CMD_END = 0x12,
  BLD_CMD_QUERY = 0x13,
  BLD_CMD_META = 0x14,
  BLD_CMD_BOOT = 0x15,
};

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

enum bld_image_state {
  BLD_IMAGE_STATE_EMPTY = 0,
  BLD_IMAGE_STATE_READY = 1,
  BLD_IMAGE_STATE_CORRUPTED = 2,
};

struct __attribute__((packed)) bld_cmd_frame {
  uint8_t sof;
  uint8_t type;
  uint16_t len;
  uint8_t cmd;
  uint8_t reserved[BLD_CMD_RESERVED_SIZE];
  uint32_t crc32;
  uint8_t eof;
};

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

struct __attribute__((packed)) bld_data_prefix {
  uint8_t sof;
  uint8_t type;
  uint16_t len;
  uint32_t seq;
  uint16_t chunk_len;
  uint8_t data[];
};

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

struct __attribute__((packed)) bld_meta_frame {
  uint8_t sof;
  uint8_t type;
  uint16_t len;
  uint32_t version;
  uint32_t size;
  uint32_t crc32_image;
  uint8_t state;
  uint32_t crc32;
  uint8_t eof;
};

static uint32_t crc32_table[256];

static void crc32_init(void) {
  for (uint32_t i = 0; i < 256u; ++i) {
    uint32_t c = i;
    for (int bit = 0; bit < 8; ++bit) {
      c = (c & 1u) ? (BLD_CRC32_POLY ^ (c >> 1)) : (c >> 1);
    }
    crc32_table[i] = c;
  }
}

static uint32_t crc32_update(uint32_t crc, const uint8_t* buf, size_t len) {
  uint32_t c = crc;
  for (size_t i = 0; i < len; ++i) {
    c = crc32_table[(c ^ buf[i]) & 0xFFu] ^ (c >> 8);
  }
  return c;
}

static uint32_t crc32_compute(const uint8_t* buf, size_t len) {
  uint32_t crc = BLD_CRC32_INITIAL;
  crc = crc32_update(crc, buf, len);
  return crc ^ BLD_CRC32_INITIAL;
}

static uint32_t frame_crc32(const uint8_t* frame_bytes, uint16_t payload_len) {
  size_t crc_len = BLD_FRAME_PREFIX_SIZE + (size_t)payload_len;
  return crc32_compute(frame_bytes, crc_len);
}

static int read_file(const char* path, uint8_t** out_buf, size_t* out_len) {
  struct stat st;
  if (path == NULL || out_buf == NULL || out_len == NULL) {
    return -1;
  }
  if (stat(path, &st) != 0) {
    perror("stat");
    return -1;
  }
  if (st.st_size <= 0) {
    fprintf(stderr, "File is empty\n");
    return -1;
  }

  size_t len = (size_t)st.st_size;
  uint8_t* buf = (uint8_t*)malloc(len);
  if (buf == NULL) {
    fprintf(stderr, "malloc failed\n");
    return -1;
  }

  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    perror("open");
    free(buf);
    return -1;
  }

  size_t off = 0u;
  while (off < len) {
    ssize_t r = read(fd, buf + off, len - off);
    if (r < 0) {
      if (errno == EINTR) {
        continue;
      }
      perror("read");
      close(fd);
      free(buf);
      return -1;
    }
    if (r == 0) {
      break;
    }
    off += (size_t)r;
  }

  close(fd);

  if (off != len) {
    fprintf(stderr, "Short read: got %zu expected %zu\n", off, len);
    free(buf);
    return -1;
  }

  *out_buf = buf;
  *out_len = len;
  return 0;
}

static speed_t baud_to_speed(int baud) {
  switch (baud) {
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
    case 460800: return B460800;
    case 921600: return B921600;
    default: return 0;
  }
}

static int serial_open(const char* dev, int baud) {
  if (dev == NULL) {
    return -1;
  }

  int fd = open(dev, O_RDWR | O_NOCTTY | O_CLOEXEC);
  if (fd < 0) {
    perror("open serial");
    return -1;
  }

  struct termios tio;
  if (tcgetattr(fd, &tio) != 0) {
    perror("tcgetattr");
    close(fd);
    return -1;
  }

  cfmakeraw(&tio);
  tio.c_cflag |= (CLOCAL | CREAD);
  tio.c_cflag &= ~CSTOPB;
  tio.c_cflag &= ~PARENB;
  tio.c_cflag &= ~CRTSCTS;

  speed_t sp = baud_to_speed(baud);
  if (sp == 0) {
    fprintf(stderr, "Unsupported baud: %d\n", baud);
    close(fd);
    return -1;
  }

  if (cfsetispeed(&tio, sp) != 0 || cfsetospeed(&tio, sp) != 0) {
    perror("cfset*speed");
    close(fd);
    return -1;
  }

  tio.c_cc[VMIN] = 0;
  tio.c_cc[VTIME] = 0;

  if (tcsetattr(fd, TCSANOW, &tio) != 0) {
    perror("tcsetattr");
    close(fd);
    return -1;
  }

  tcflush(fd, TCIOFLUSH);
  return fd;
}

static int write_all(int fd, const uint8_t* buf, size_t len) {
  if (buf == NULL) {
    return -1;
  }

  size_t off = 0u;
  while (off < len) {
    ssize_t written = write(fd, buf + off, len - off);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      perror("write");
      return -1;
    }
    off += (size_t)written;
  }
  return 0;
}

static ssize_t read_timeout(int fd, uint8_t* buf, size_t max_len, int timeout_ms) {
  if (buf == NULL || max_len == 0u) {
    return -1;
  }

  struct pollfd pfd = {.fd = fd, .events = POLLIN};
  int pr = poll(&pfd, 1, timeout_ms);
  if (pr < 0) {
    if (errno == EINTR) {
      return 0;
    }
    perror("poll");
    return -1;
  }
  if (pr == 0) {
    return 0;
  }
  if ((pfd.revents & POLLIN) != 0) {
    ssize_t r = read(fd, buf, max_len);
    if (r < 0) {
      if (errno == EINTR) {
        return 0;
      }
      perror("read");
      return -1;
    }
    return r;
  }
  return 0;
}

static int send_cmd(int fd, enum bld_cmd cmd) {
  struct bld_cmd_frame frame;
  memset(&frame, 0, sizeof(frame));
  frame.sof = BLD_SOF;
  frame.type = BLD_PKT_CMD;
  frame.len = 4u;
  frame.cmd = (uint8_t)cmd;
  frame.crc32 = frame_crc32((const uint8_t*)&frame, frame.len);
  frame.eof = BLD_EOF;
  return write_all(fd, (const uint8_t*)&frame, sizeof(frame));
}

static int send_header(int fd, uint32_t image_size, uint32_t image_crc32, uint32_t version) {
  struct bld_header_frame frame;
  memset(&frame, 0, sizeof(frame));
  frame.sof = BLD_SOF;
  frame.type = BLD_PKT_HEADER;
  frame.len = 12u;
  frame.image_size = image_size;
  frame.image_crc32 = image_crc32;
  frame.version = version;
  frame.crc32 = frame_crc32((const uint8_t*)&frame, frame.len);
  frame.eof = BLD_EOF;
  return write_all(fd, (const uint8_t*)&frame, sizeof(frame));
}

static int send_data(int fd, uint32_t seq, const uint8_t* chunk, uint16_t chunk_len) {
  if (chunk == NULL || chunk_len == 0u) {
    return -1;
  }

  uint16_t payload_len = (uint16_t)(BLD_DATA_PREFIX_PAYLOAD_SIZE + chunk_len);
  size_t total_size = BLD_FRAME_PREFIX_SIZE + (size_t)payload_len +
                      BLD_FRAME_CRC32_SIZE + BLD_FRAME_EOF_SIZE;

  uint8_t* frame = (uint8_t*)malloc(total_size);
  if (frame == NULL) {
    return -1;
  }

  size_t offset = 0u;
  frame[offset++] = BLD_SOF;
  frame[offset++] = BLD_PKT_DATA;
  memcpy(&frame[offset], &payload_len, sizeof(payload_len));
  offset += sizeof(payload_len);
  memcpy(&frame[offset], &seq, sizeof(seq));
  offset += sizeof(seq);
  memcpy(&frame[offset], &chunk_len, sizeof(chunk_len));
  offset += sizeof(chunk_len);
  memcpy(&frame[offset], chunk, chunk_len);
  offset += chunk_len;

  uint32_t crc32 = frame_crc32(frame, payload_len);
  memcpy(&frame[offset], &crc32, sizeof(crc32));
  offset += sizeof(crc32);
  frame[offset++] = BLD_EOF;

  int rc = write_all(fd, frame, total_size);
  free(frame);
  return rc;
}

static int discard_frame_tail(int fd, size_t byte_count, int timeout_ms) {
  uint8_t tmp[BLD_HOST_DROP_BUF_SIZE];
  size_t remaining = byte_count;

  while (remaining > 0u) {
    size_t chunk = (remaining > sizeof(tmp)) ? sizeof(tmp) : remaining;
    ssize_t r = read_timeout(fd, tmp, chunk, timeout_ms);
    if (r <= 0) {
      return -1;
    }
    remaining -= (size_t)r;
  }
  return 0;
}

static int recv_status(int fd, struct bld_status_frame* out, int timeout_ms) {
  if (out == NULL) {
    return -1;
  }

  uint8_t byte = 0u;
  int elapsed_ms = 0;

  while (true) {
    ssize_t r = read_timeout(fd, &byte, 1u, BLD_HOST_POLL_STEP_MS);
    if (r < 0) {
      return -1;
    }
    if (r == 0) {
      elapsed_ms += BLD_HOST_POLL_STEP_MS;
      if (elapsed_ms >= timeout_ms) {
        return -2;
      }
      continue;
    }
    if (byte == BLD_SOF) {
      break;
    }
  }

  uint8_t hdr[3];
  ssize_t r = read_timeout(fd, hdr, sizeof(hdr), timeout_ms);
  if (r < 0) {
    return -1;
  }
  if (r != (ssize_t)sizeof(hdr)) {
    return -2;
  }

  uint8_t type = hdr[0];
  uint16_t len = 0u;
  memcpy(&len, &hdr[1], sizeof(len));

  if (type != BLD_PKT_STATUS) {
    if (discard_frame_tail(fd, (size_t)len + BLD_FRAME_CRC32_SIZE + BLD_FRAME_EOF_SIZE,
                           timeout_ms) != 0) {
      return -1;
    }
    return -3;
  }

  size_t rest_size = (size_t)len + BLD_FRAME_CRC32_SIZE + BLD_FRAME_EOF_SIZE;
  uint8_t* rest = (uint8_t*)malloc(rest_size);
  if (rest == NULL) {
    return -1;
  }

  size_t got = 0u;
  while (got < rest_size) {
    ssize_t rr = read_timeout(fd, rest + got, rest_size - got, timeout_ms);
    if (rr < 0) {
      free(rest);
      return -1;
    }
    if (rr == 0) {
      free(rest);
      return -2;
    }
    got += (size_t)rr;
  }

  size_t crc_input_len = BLD_FRAME_PREFIX_SIZE + (size_t)len;
  uint8_t* crc_buf = (uint8_t*)malloc(crc_input_len);
  if (crc_buf == NULL) {
    free(rest);
    return -1;
  }

  crc_buf[0] = BLD_SOF;
  crc_buf[1] = type;
  memcpy(&crc_buf[2], &len, sizeof(len));
  memcpy(&crc_buf[4], rest, len);

  uint32_t rx_crc32 = 0u;
  memcpy(&rx_crc32, rest + len, sizeof(rx_crc32));
  uint8_t rx_eof = rest[len + BLD_FRAME_CRC32_SIZE];
  uint32_t calc_crc32 = crc32_compute(crc_buf, crc_input_len);

  free(crc_buf);

  if (rx_eof != BLD_EOF) {
    free(rest);
    return -4;
  }
  if (calc_crc32 != rx_crc32) {
    free(rest);
    return -5;
  }

  memset(out, 0, sizeof(*out));
  out->sof = BLD_SOF;
  out->type = type;
  out->len = len;
  if (len >= BLD_FRAME_STATUS_PAYLOAD_SIZE) {
    out->status = rest[0];
    out->state = rest[1];
    memcpy(&out->reserved, rest + 2, sizeof(out->reserved));
    memcpy(&out->detail, rest + 4, sizeof(out->detail));
  }
  out->crc32 = rx_crc32;
  out->eof = rx_eof;

  free(rest);
  return 0;
}

static int recv_meta(int fd, struct bld_meta_frame* out, int timeout_ms) {
  if (out == NULL) {
    return -1;
  }

  uint8_t byte = 0u;
  int elapsed_ms = 0;

  while (true) {
    ssize_t r = read_timeout(fd, &byte, 1u, BLD_HOST_POLL_STEP_MS);
    if (r < 0) {
      return -1;
    }
    if (r == 0) {
      elapsed_ms += BLD_HOST_POLL_STEP_MS;
      if (elapsed_ms >= timeout_ms) {
        return -2;
      }
      continue;
    }
    if (byte == BLD_SOF) {
      break;
    }
  }

  uint8_t hdr[3];
  ssize_t r = read_timeout(fd, hdr, sizeof(hdr), timeout_ms);
  if (r < 0) {
    return -1;
  }
  if (r != (ssize_t)sizeof(hdr)) {
    return -2;
  }

  uint8_t type = hdr[0];
  uint16_t len = 0u;
  memcpy(&len, &hdr[1], sizeof(len));

  if (type != BLD_PKT_META) {
    if (discard_frame_tail(fd, (size_t)len + BLD_FRAME_CRC32_SIZE + BLD_FRAME_EOF_SIZE,
                           timeout_ms) != 0) {
      return -1;
    }
    return -3;
  }

  size_t rest_size = (size_t)len + BLD_FRAME_CRC32_SIZE + BLD_FRAME_EOF_SIZE;
  uint8_t* rest = (uint8_t*)malloc(rest_size);
  if (rest == NULL) {
    return -1;
  }

  size_t got = 0u;
  while (got < rest_size) {
    ssize_t rr = read_timeout(fd, rest + got, rest_size - got, timeout_ms);
    if (rr < 0) {
      free(rest);
      return -1;
    }
    if (rr == 0) {
      free(rest);
      return -2;
    }
    got += (size_t)rr;
  }

  size_t crc_input_len = BLD_FRAME_PREFIX_SIZE + (size_t)len;
  uint8_t* crc_buf = (uint8_t*)malloc(crc_input_len);
  if (crc_buf == NULL) {
    free(rest);
    return -1;
  }

  crc_buf[0] = BLD_SOF;
  crc_buf[1] = type;
  memcpy(&crc_buf[2], &len, sizeof(len));
  memcpy(&crc_buf[4], rest, len);

  uint32_t rx_crc32 = 0u;
  memcpy(&rx_crc32, rest + len, sizeof(rx_crc32));
  uint8_t rx_eof = rest[len + BLD_FRAME_CRC32_SIZE];
  uint32_t calc_crc32 = crc32_compute(crc_buf, crc_input_len);

  free(crc_buf);

  if (rx_eof != BLD_EOF) {
    free(rest);
    return -4;
  }
  if (calc_crc32 != rx_crc32) {
    free(rest);
    return -5;
  }

  memset(out, 0, sizeof(*out));
  out->sof = BLD_SOF;
  out->type = type;
  out->len = len;
  if (len >= BLD_FRAME_META_PAYLOAD_SIZE) {
    memcpy(&out->version, rest + 0, sizeof(out->version));
    memcpy(&out->size, rest + 4, sizeof(out->size));
    memcpy(&out->crc32_image, rest + 8, sizeof(out->crc32_image));
    memcpy(&out->state, rest + 12, sizeof(out->state));
  }
  out->crc32 = rx_crc32;
  out->eof = rx_eof;

  free(rest);
  return 0;
}

static const char* status_to_string(uint8_t status) {
  switch ((enum bld_status)status) {
    case BLD_ST_OK: return "OK";
    case BLD_ST_ERR: return "ERR";
    case BLD_ST_BAD_FRAME: return "BAD_FRAME";
    case BLD_ST_BAD_CRC: return "BAD_CRC";
    case BLD_ST_TOO_LARGE: return "TOO_LARGE";
    case BLD_ST_BAD_STATE: return "BAD_STATE";
    case BLD_ST_FLASH_ERR: return "FLASH_ERR";
    case BLD_ST_SEQ_ERR: return "SEQ_ERR";
    case BLD_ST_BOOT_ERR: return "BOOT_ERR";
    default: return "UNKNOWN";
  }
}

static const char* image_state_to_string(uint8_t state) {
  switch ((enum bld_image_state)state) {
    case BLD_IMAGE_STATE_EMPTY: return "EMPTY";
    case BLD_IMAGE_STATE_READY: return "READY";
    case BLD_IMAGE_STATE_CORRUPTED: return "CORRUPTED";
    default: return "UNKNOWN";
  }
}

static int expect_ok_status(int fd, int timeout_ms, const char* where) {
  struct bld_status_frame frame;
  int rc = recv_status(fd, &frame, timeout_ms);
  if (rc == -2) {
    fprintf(stderr, "%s: timeout waiting STATUS\n", where);
    return -1;
  }
  if (rc != 0) {
    fprintf(stderr, "%s: failed to parse STATUS (rc=%d)\n", where, rc);
    return -1;
  }
  if (frame.status != BLD_ST_OK) {
    fprintf(stderr,
            "%s: STATUS=%s(%u) state=%u detail=0x%08" PRIx32 "\n",
            where,
            status_to_string(frame.status),
            frame.status,
            frame.state,
            frame.detail);
    return -1;
  }
  return 0;
}

static int do_query(int fd, int timeout_ms) {
  struct bld_status_frame frame;
  if (send_cmd(fd, BLD_CMD_QUERY) != 0) {
    return -1;
  }
  if (recv_status(fd, &frame, timeout_ms) != 0) {
    fprintf(stderr, "query: failed to receive STATUS\n");
    return -1;
  }
  printf("STATUS=%s(%u) state=%u detail=0x%08" PRIx32 "\n",
         status_to_string(frame.status),
         frame.status,
         frame.state,
         frame.detail);
  return 0;
}

static int do_abort(int fd, int timeout_ms) {
  if (send_cmd(fd, BLD_CMD_ABORT) != 0) {
    return -1;
  }
  return expect_ok_status(fd, timeout_ms, "abort");
}

static int do_meta(int fd, int timeout_ms, bool verbose) {
  struct bld_meta_frame frame;
  if (send_cmd(fd, BLD_CMD_META) != 0) {
    return -1;
  }
  if (recv_meta(fd, &frame, timeout_ms) != 0) {
    fprintf(stderr, "meta: failed to receive META\n");
    return -1;
  }

  if (verbose) {
    fprintf(stderr, "META frame received\n");
  }

  printf("version=%" PRIu32 " size=%" PRIu32 " crc32=0x%08" PRIx32 " state=%s(%u)\n",
         frame.version,
         frame.size,
         frame.crc32_image,
         image_state_to_string(frame.state),
         frame.state);
  return 0;
}

static int do_write(int fd,
                    const char* firmware_path,
                    uint32_t version,
                    uint16_t chunk_size,
                    int timeout_ms,
                    bool verbose) {
  uint8_t* fw = NULL;
  size_t fw_len = 0u;
  if (read_file(firmware_path, &fw, &fw_len) != 0) {
    return -1;
  }

  uint32_t image_crc32 = crc32_compute(fw, fw_len);
  uint32_t image_size = (uint32_t)fw_len;

  if (verbose) {
    fprintf(stderr, "Image: %s\n", firmware_path);
    fprintf(stderr, "  size = %" PRIu32 "\n", image_size);
    fprintf(stderr, "  crc32 = 0x%08" PRIx32 "\n", image_crc32);
    fprintf(stderr, "  version = 0x%08" PRIx32 "\n", version);
    fprintf(stderr, "  chunk = %" PRIu16 "\n", chunk_size);
  }

  struct bld_meta_frame meta;
  if (send_cmd(fd, BLD_CMD_META) == 0 && recv_meta(fd, &meta, timeout_ms) == 0) {
    if (verbose) {
      fprintf(stderr, "  reveived_size = %" PRIu32 "\n", meta.size);
      fprintf(stderr, "  received_crc32 = 0x%08" PRIx32 "\n", meta.crc32_image);
      fprintf(stderr, "  received_version = 0x%08" PRIx32 "\n", meta.version);
      fprintf(stderr, "  received_status = %" PRIu8 "\n", meta.state);
    }
    if (meta.crc32_image == image_crc32 &&
        meta.size == image_size &&
        meta.version == version &&
        meta.state == (uint8_t)BLD_IMAGE_STATE_READY) {
      fprintf(stderr, "Image already present and ready\n");
      free(fw);
      return 0;
    }
  }

  if (send_cmd(fd, BLD_CMD_START) != 0 ||
      expect_ok_status(fd, timeout_ms, "start") != 0) {
    free(fw);
    return -1;
  }

  if (send_header(fd, image_size, image_crc32, version) != 0 ||
      expect_ok_status(fd, timeout_ms, "header") != 0) {
    free(fw);
    return -1;
  }

  uint32_t seq = 0u;
  size_t off = 0u;
  while (off < fw_len) {
    uint16_t chunk_len = chunk_size;
    if (fw_len - off < (size_t)chunk_len) {
      chunk_len = (uint16_t)(fw_len - off);
    }

    if (send_data(fd, seq, fw + off, chunk_len) != 0 ||
        expect_ok_status(fd, timeout_ms, "data") != 0) {
      fprintf(stderr, "Failed at seq=%" PRIu32 " off=%zu\n", seq, off);
      free(fw);
      return -1;
    }

    off += chunk_len;
    ++seq;

    if (verbose && (seq % 64u == 0u)) {
      double pct = (fw_len == 0u) ? 0.0 : (100.0 * (double)off / (double)fw_len);
      fprintf(stderr, "Progress: %zu/%zu (%.1f%%)\n", off, fw_len, pct);
    }
  }

  if (send_cmd(fd, BLD_CMD_END) != 0 ||
      expect_ok_status(fd, timeout_ms, "end") != 0) {
    free(fw);
    return -1;
  }

  if (send_cmd(fd, BLD_CMD_BOOT) != 0 ||
      expect_ok_status(fd, timeout_ms, "boot") != 0) {
    free(fw);
    return -1;
  }

  free(fw);
  return 0;
}

static void usage(const char* prog) {
  fprintf(stderr,
          "Usage:\n"
          "  %s -d <device> [-B baud] [-c chunk] [-t ms] [-v hexver] [-V] <cmd> [args]\n"
          "\n"
          "Commands:\n"
          "  write <firmware.bin>   Send START + HEADER + DATA + END + BOOT\n"
          "  query                  Send QUERY and print STATUS\n"
          "  abort                  Send ABORT\n"
          "  meta                   Send META and print metadata\n"
          "\n"
          "Options:\n"
          "  -d <device>   Serial device (for example /dev/ttyACM0)\n"
          "  -B <baud>     Baud rate (default %d)\n"
          "  -c <chunk>    Data chunk size in bytes (default %u)\n"
          "  -t <ms>       Response timeout in milliseconds (default %d)\n"
          "  -v <hex>      Firmware version for HEADER (default 0x%08x)\n"
          "  -V            Verbose output\n",
          prog,
          BLD_HOST_DEFAULT_BAUD,
          BLD_HOST_DEFAULT_CHUNK,
          BLD_HOST_DEFAULT_TIMEOUT_MS,
          BLD_HOST_DEFAULT_VERSION);
}

int main(int argc, char** argv) {
  const char* device = NULL;
  int baud = BLD_HOST_DEFAULT_BAUD;
  uint16_t chunk = BLD_HOST_DEFAULT_CHUNK;
  int timeout_ms = BLD_HOST_DEFAULT_TIMEOUT_MS;
  uint32_t version = BLD_HOST_DEFAULT_VERSION;
  bool verbose = false;

  crc32_init();

  int opt = 0;
  while ((opt = getopt(argc, argv, "d:B:c:t:v:Vh")) != -1) {
    switch (opt) {
      case 'd':
        device = optarg;
        break;
      case 'B':
        baud = atoi(optarg);
        break;
      case 'c':
        chunk = (uint16_t)strtoul(optarg, NULL, 0);
        break;
      case 't':
        timeout_ms = atoi(optarg);
        break;
      case 'v':
        version = (uint32_t)strtoul(optarg, NULL, 0);
        break;
      case 'V':
        verbose = true;
        break;
      case 'h':
      default:
        usage(argv[0]);
        return 0;
    }
  }

  if (device == NULL || optind >= argc) {
    usage(argv[0]);
    return 1;
  }

  const char* cmd = argv[optind++];

  int fd = serial_open(device, baud);
  if (fd < 0) {
    return 1;
  }

  int rc = 0;
  if (strcmp(cmd, "write") == 0) {
    if (optind >= argc) {
      fprintf(stderr, "write: missing firmware file\n");
      rc = 1;
    } else {
      rc = (do_write(fd, argv[optind], version, chunk, timeout_ms, verbose) == 0) ? 0 : 1;
    }
  } else if (strcmp(cmd, "query") == 0) {
    rc = (do_query(fd, timeout_ms) == 0) ? 0 : 1;
  } else if (strcmp(cmd, "abort") == 0) {
    rc = (do_abort(fd, timeout_ms) == 0) ? 0 : 1;
  } else if (strcmp(cmd, "meta") == 0) {
    rc = (do_meta(fd, timeout_ms, verbose) == 0) ? 0 : 1;
  } else {
    fprintf(stderr, "Unknown command: %s\n", cmd);
    usage(argv[0]);
    rc = 1;
  }

  close(fd);
  return rc;
}