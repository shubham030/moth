/* WiFi bring-up for hot push. See hotpush.h for the contract.
 *
 * Everything here is deliberately fire-and-forget: app_main calls
 * hotpush_net_start() once, the event handlers keep the connection alive, and
 * the rest of the firmware only ever asks "connected?" and "what IP?". A
 * board with no credentials, or out of range, runs its UI exactly as before —
 * the network is a bonus, never a dependency.
 */
#include "hotpush.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "hotpush";

static volatile bool s_connected;
static char s_ip[16] = "0.0.0.0";

/* What the radio can actually see, for when the configured SSID cannot be
 * found: the usual cause is a 5GHz-only network — this chip is 2.4GHz — or a
 * typo, and one look at this list settles which. */
static void log_visible_networks(void) {
  uint16_t n = 0;
  esp_wifi_scan_get_ap_num(&n);
  if (n == 0) {
    ESP_LOGW(TAG, "scan: no 2.4GHz networks visible at all");
    return;
  }
  if (n > 12) n = 12;
  /* Static: a kilobyte of records does not fit the WiFi event task's stack,
   * and this runs on it. Single-threaded by construction — events arrive one
   * at a time. */
  static wifi_ap_record_t recs[12];
  if (esp_wifi_scan_get_ap_records(&n, recs) != ESP_OK) return;
  ESP_LOGW(TAG, "scan: %d networks visible on 2.4GHz:", n);
  for (int i = 0; i < n; i++) {
    ESP_LOGW(TAG, "  '%s' (rssi %d, channel %d)", (const char *)recs[i].ssid,
             recs[i].rssi, recs[i].primary);
  }
}

static bool load_creds(char *ssid, size_t ssid_cap, char *pass, size_t pass_cap) {
  nvs_handle_t h;
  if (nvs_open("moth", NVS_READONLY, &h) != ESP_OK) return false;
  size_t sl = ssid_cap, pl = pass_cap;
  esp_err_t a = nvs_get_str(h, "wifi_ssid", ssid, &sl);
  esp_err_t b = nvs_get_str(h, "wifi_pass", pass, &pl);
  nvs_close(h);
  return a == ESP_OK && b == ESP_OK && ssid[0] != '\0';
}

static void on_net_event(void *arg, esp_event_base_t base, int32_t id,
                         void *data) {
  (void)arg;
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    s_connected = false;
    strcpy(s_ip, "0.0.0.0");
    /* Retry forever; each attempt takes a scan's worth of time, so this does
     * not spin. Log occasionally rather than on every miss — a board left
     * overnight out of range should not fill the console. The reason code is
     * the diagnosis: 201 means the AP was not seen at all (wrong SSID, out
     * of range, or a 5GHz-only network — this chip is 2.4GHz), 15 means the
     * handshake timed out, which is almost always a wrong password. */
    const wifi_event_sta_disconnected_t *d = data;
    static int misses;
    if (++misses % 10 == 1) {
      ESP_LOGW(TAG, "wifi disconnected (reason %d), retrying (attempt %d)",
               d->reason, misses);
    }
    /* The AP was never seen: scan once and say what is visible, then go
     * back to retrying. The scan's completion re-triggers the connect — but
     * only if the scan actually started. Returning on a failed start would
     * leave nobody to call connect again, and "retry forever" would quietly
     * become "retried once". */
    if (d->reason == WIFI_REASON_NO_AP_FOUND && misses == 1 &&
        esp_wifi_scan_start(NULL, false) == ESP_OK) {
      return;
    }
    esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
    log_visible_networks();
    esp_wifi_connect();
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    const ip_event_got_ip_t *e = data;
    snprintf(s_ip, sizeof s_ip, IPSTR, IP2STR(&e->ip_info.ip));
    s_connected = true;
    ESP_LOGI(TAG, "connected — push with: mothc app.dart --push %s:%d", s_ip,
             HOTPUSH_PORT);
  }
}

/* True on success; logs and reports failure otherwise. Nothing in WiFi
 * bring-up may abort: by the time this runs, the panel, its DMA buffers and
 * the renderer have claimed internal RAM, so ESP_ERR_NO_MEM is a realistic
 * outcome — and a panic here turns a board whose UI ran fine into a
 * blank-screen reboot loop over an optional feature. The contract at the top
 * of hotpush.h — the network is a bonus, never a dependency — applies to the
 * failure path most of all. */
static bool net_step(esp_err_t err, const char *what) {
  if (err == ESP_OK) return true;
  ESP_LOGW(TAG, "%s failed (%s) — hot push over wifi disabled", what,
           esp_err_to_name(err));
  return false;
}

void hotpush_net_start(void) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    /* A layout upgrade wipes NVS — and the credentials with it. Say so, or
     * the board silently stops connecting after an IDF bump. */
    ESP_LOGW(TAG, "NVS layout changed; erasing — wifi needs re-provisioning");
    if (!net_step(nvs_flash_erase(), "nvs erase")) return;
    err = nvs_flash_init();
  }
  if (!net_step(err, "nvs init")) return;

  char ssid[33] = {0}, pass[65] = {0};
  if (!load_creds(ssid, sizeof ssid, pass, sizeof pass)) {
    ESP_LOGW(TAG, "no wifi credentials in NVS — hot push disabled. "
                  "Provision with: python3 tools/provision/provision.py");
    return;
  }

  if (!net_step(esp_netif_init(), "netif init")) return;
  err = esp_event_loop_create_default();
  /* INVALID_STATE means a loop already exists, which is fine — some other
   * subsystem got there first. */
  if (err != ESP_ERR_INVALID_STATE && !net_step(err, "event loop")) return;
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  if (!net_step(esp_wifi_init(&init), "wifi init")) return;
  if (!net_step(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                           on_net_event, NULL),
                "wifi handler") ||
      !net_step(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                           on_net_event, NULL),
                "ip handler")) {
    return;
  }

  wifi_config_t wc = {0};
  /* Full width, no reserved NUL: ssid and password are length-implied byte
   * arrays, and wc is zero-initialized. Reserving a terminator truncated a
   * 32-char SSID and the standard 64-hex-digit raw WPA2 PSK by one character
   * — which then failed as reason 15, indistinguishable from a typo. */
  size_t sl = strlen(ssid);
  if (sl > sizeof wc.sta.ssid) sl = sizeof wc.sta.ssid;
  memcpy(wc.sta.ssid, ssid, sl);
  size_t pl = strlen(pass);
  if (pl > sizeof wc.sta.password) pl = sizeof wc.sta.password;
  memcpy(wc.sta.password, pass, pl);
  /* WPA and up when there is a password; an open network only when the
   * provisioned password is empty, so a typo cannot silently join an
   * unsecured twin of the real AP. */
  wc.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
  if (!net_step(esp_wifi_set_mode(WIFI_MODE_STA), "sta mode") ||
      !net_step(esp_wifi_set_config(WIFI_IF_STA, &wc), "sta config") ||
      !net_step(esp_wifi_start(), "wifi start")) {
    return;
  }
  ESP_LOGI(TAG, "wifi connecting to '%s'", ssid);
}

bool hotpush_net_connected(void) { return s_connected; }

const char *hotpush_net_ip(void) { return s_ip; }
