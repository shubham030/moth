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
  uint32_t nonce;         /* from the frame being received */
  bool awaiting_verdict;  /* poll returned a blob; respond has not run */
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
  if (!p || p->awaiting_verdict) return NULL;

  if (p->client < 0) {
    int c = accept(p->listener, NULL, NULL);
    if (c < 0) return NULL; /* nothing waiting, or a transient failure */
    set_nonblocking(c);
#ifdef SO_NOSIGPIPE
    /* The verdict reply is this file's only write; a peer that closed first
     * must cost an EPIPE errno, not a process-killing SIGPIPE. Linux and
     * lwIP get the same via MSG_NOSIGNAL at the send. */
    int nosig = 1;
    setsockopt(c, SOL_SOCKET, SO_NOSIGPIPE, &nosig, sizeof nosig);
#endif
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
    p->nonce = mpsh_header_nonce(p->header);
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
  /* The client stays open: the reply is the VERDICT, and the verdict does
   * not exist until the caller has verified the blob. An earlier version
   * acked on receipt, which told the sender "pushed" for a blob the host
   * then rejected. */
  p->awaiting_verdict = true;
  return complete;
}

void moth_push_respond(moth_push *p, bool ok) {
  if (!p || !p->awaiting_verdict) return;
  /* The verdict flag clears even when the peer is already gone — otherwise
   * a client that vanished mid-verify would leave the channel refusing every
   * future push. */
  if (p->client >= 0) {
    uint8_t reply[MPSH_REPLY_LEN];
    mpsh_make_reply(reply, ok, p->nonce);
#ifdef MSG_NOSIGNAL
    const int flags = MSG_NOSIGNAL;
#else
    const int flags = 0;
#endif
    /* Nonblocking socket: one EAGAIN must not eat the verdict, since the
     * sender has no other way to learn it. Elapsed-time form, not an
     * absolute deadline — now_ms() wraps every 49.7 days and an absolute
     * `now + 500` near the wrap never admits even one iteration (the stall
     * check 75 lines up already does it right). Yield on EAGAIN rather
     * than spinning a core against a full buffer. */
    size_t off = 0;
    const uint32_t start = now_ms();
    while (off < sizeof reply && now_ms() - start < 500) {
      ssize_t n = send(p->client, reply + off, sizeof reply - off, flags);
      if (n > 0) {
        off += (size_t)n;
      } else if (n < 0 && !read_would_block()) {
        break; /* peer is gone; it just misses its reply */
      } else {
        usleep(1000);
      }
    }
  }
  p->awaiting_verdict = false;
  drop_client(p);
}

void moth_push_abandon(moth_push *p) {
  if (!p || !p->awaiting_verdict) return;
  p->awaiting_verdict = false;
  drop_client(p);
}

void moth_push_close(moth_push *p) {
  if (!p) return;
  drop_client(p);
  close(p->listener);
  free(p);
}
