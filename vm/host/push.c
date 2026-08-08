#include "push.h"

#include <errno.h>
#include <stdbool.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PUSH_MAGIC "MPSH"
#define HEADER_LEN 8
#define MAX_BLOB (1u << 20) /* a megabyte is far beyond any real program */

struct moth_push {
  int listener;
  int client;
  uint8_t header[HEADER_LEN];
  size_t header_got;
  uint8_t *blob;
  size_t blob_len, blob_got;
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
  }

  if (p->header_got < HEADER_LEN) {
    ssize_t n = read(p->client, p->header + p->header_got, HEADER_LEN - p->header_got);
    if (n == 0) { drop_client(p); return NULL; }
    if (n < 0) {
      if (!read_would_block()) drop_client(p);
      return NULL;
    }
    p->header_got += (size_t)n;
    if (p->header_got < HEADER_LEN) return NULL;

    if (memcmp(p->header, PUSH_MAGIC, 4) != 0) { drop_client(p); return NULL; }
    p->blob_len = (size_t)p->header[4] | ((size_t)p->header[5] << 8) |
                  ((size_t)p->header[6] << 16) | ((size_t)p->header[7] << 24);
    if (p->blob_len == 0 || p->blob_len > MAX_BLOB) { drop_client(p); return NULL; }
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
  }

  uint8_t *complete = p->blob;
  *len_out = p->blob_len;
  p->blob = NULL; /* ownership passes to the caller */
  drop_client(p);
  return complete;
}

void moth_push_close(moth_push *p) {
  if (!p) return;
  drop_client(p);
  close(p->listener);
  free(p);
}
