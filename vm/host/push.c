#include "push.h"
#include "push_proto.h"

#include <errno.h>
#include <stdbool.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define HEADER_LEN MPSH_HEADER_LEN

/* A peer that disappears without a FIN or an RST — a board losing power, a
 * laptop closing its lid mid-push — leaves a socket that never reports an
 * error and never delivers a byte. Without a deadline it would hold the
 * channel forever and no later push could be accepted. Progress resets it,
 * so this only fires on a connection that has gone completely silent. */
#define PUSH_STALL_MS 5000

static uint32_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

struct moth_push {
  int listener;
  int client;
  uint8_t header[HEADER_LEN];
  size_t header_got;
  uint8_t *blob;
  size_t blob_len, blob_got;
  uint32_t last_progress_ms;
};

static void set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* True when a failed read simply had nothing to give. Anything else — a reset
 * connection, a client killed mid-transfer — has to drop the client, or the
 * channel stays occupied by a peer that will never send another byte and no
 * further push is ever accepted. */
static bool read_would_block(void) {
  return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
}

static void drop_client(moth_push *p) {
  if (p->client >= 0) close(p->client);
  p->client = -1;
  p->header_got = 0;
  free(p->blob);
  p->blob = NULL;
  p->blob_len = 0;
  p->blob_got = 0;
}

moth_push *moth_push_listen(int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return NULL;
  int yes = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons((uint16_t)port);
  if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0 || listen(fd, 1) < 0) {
    close(fd);
    return NULL;
  }
  set_nonblocking(fd);

  moth_push *p = calloc(1, sizeof *p);
  if (!p) { close(fd); return NULL; }
  p->listener = fd;
  p->client = -1;
  return p;
}

uint8_t *moth_push_poll(moth_push *p, size_t *len_out) {
  if (!p) return NULL;

  if (p->client < 0) {
    int c = accept(p->listener, NULL, NULL);
    if (c < 0) return NULL; /* nothing waiting, or a transient failure */
    set_nonblocking(c);
    p->client = c;
    p->last_progress_ms = now_ms();
  } else if (now_ms() - p->last_progress_ms > PUSH_STALL_MS) {
    fprintf(stderr, "push: client stalled, dropping it\n");
    fflush(stderr);
    drop_client(p);
    return NULL;
  }

  if (p->header_got < HEADER_LEN) {
    ssize_t n = read(p->client, p->header + p->header_got, HEADER_LEN - p->header_got);
    if (n == 0) { drop_client(p); return NULL; }
    if (n < 0) {
      if (!read_would_block()) drop_client(p);
      return NULL;
    }
    p->header_got += (size_t)n;
    p->last_progress_ms = now_ms();
    if (p->header_got < HEADER_LEN) return NULL;

    if (memcmp(p->header, MPSH_MAGIC, MPSH_MAGIC_LEN) != 0) {
      drop_client(p);
      return NULL;
    }
    p->blob_len = mpsh_header_len(p->header);
    if (!mpsh_len_ok(p->blob_len)) { drop_client(p); return NULL; }
    p->blob = malloc(p->blob_len);
    if (!p->blob) { drop_client(p); return NULL; }
    p->blob_got = 0;
  }

  while (p->blob_got < p->blob_len) {
    ssize_t n = read(p->client, p->blob + p->blob_got, p->blob_len - p->blob_got);
    if (n == 0) { drop_client(p); return NULL; }
    if (n < 0) {
      /* Partway through a blob: a dropped peer means the half-read blob is
       * junk, so the whole transfer is abandoned rather than resumed. */
      if (!read_would_block()) drop_client(p);
      return NULL;
    }
    p->blob_got += (size_t)n;
    p->last_progress_ms = now_ms();
  }

  uint8_t *complete = p->blob;
  *len_out = p->blob_len;
  p->blob = NULL; /* ownership passes to the caller */
  /* Best-effort receipt, so the sender can tell "delivered intact" from
   * "connected to something that swallowed the bytes". Verification happens
   * after this returns; a rejected blob is still reported on the host's log,
   * which the serial transport reads back as its ack. */
  (void)write(p->client, "ok\n", 3);
  drop_client(p);
  return complete;
}

void moth_push_close(moth_push *p) {
  if (!p) return;
  drop_client(p);
  close(p->listener);
  free(p);
}
