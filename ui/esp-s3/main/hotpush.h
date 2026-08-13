/* WiFi for hot push: brings the station up from NVS credentials and keeps it
 * connected. Credentials are never compiled in — they live in the NVS
 * partition, written from the host by tools/provision/provision.py, and
 * survive app reflashes because `idf.py flash` does not touch that partition.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* The port the board listens on for `mothc app.dart --push <ip>:7621`. */
#define HOTPUSH_PORT 7621

/* Starts WiFi if credentials exist in NVS; logs how to provision if not.
 * Returns immediately either way — connection happens in the background and
 * retries forever, so the UI never waits on the network. */
void hotpush_net_start(void);

/* Reads the 32-byte pairing key provision.py stored in NVS. The three
 * outcomes are NOT collapsible: "never paired" opens the port with a
 * warning, but "paired and unreadable" must REFUSE network pushes — a
 * security posture that degrades open on storage trouble isn't one. */
typedef enum {
  HOTPUSH_KEY_OK,     /* out[] holds the pairing key */
  HOTPUSH_KEY_ABSENT, /* never provisioned — genuinely unpaired */
  HOTPUSH_KEY_FAULT,  /* a key exists or existed and cannot be read */
} hotpush_key_state;
hotpush_key_state hotpush_load_push_key(uint8_t out[32]);

/* True while the station has an IP. */
bool hotpush_net_connected(void);

/* Dotted-quad IP while connected, "0.0.0.0" otherwise. */
const char *hotpush_net_ip(void);
