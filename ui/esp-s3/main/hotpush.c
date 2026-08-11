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
     * overnight out of range should not fill the console. */
    static int misses;
    if (++misses % 10 == 1) {
      ESP_LOGW(TAG, "wifi disconnected, retrying (attempt %d)", misses);
    }
    esp_wifi_connect();
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    const ip_event_got_ip_t *e = data;
    snprintf(s_ip, sizeof s_ip, IPSTR, IP2STR(&e->ip_info.ip));
    s_connected = true;
    ESP_LOGI(TAG, "connected — push with: mothc app.dart --push %s:%d", s_ip,
             HOTPUSH_PORT);
  }
}

void hotpush_net_start(void) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    /* A layout upgrade wipes NVS — and the credentials with it. Say so, or
     * the board silently stops connecting after an IDF bump. */
    ESP_LOGW(TAG, "NVS layout changed; erasing — wifi needs re-provisioning");
    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(nvs_flash_init());
  } else {
    ESP_ERROR_CHECK(err);
  }

  char ssid[33] = {0}, pass[65] = {0};
  if (!load_creds(ssid, sizeof ssid, pass, sizeof pass)) {
    ESP_LOGW(TAG, "no wifi credentials in NVS — hot push disabled. "
                  "Provision with: python3 tools/provision/provision.py");
    return;
  }

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&init));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             on_net_event, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             on_net_event, NULL));

  wifi_config_t wc = {0};
  strncpy((char *)wc.sta.ssid, ssid, sizeof wc.sta.ssid - 1);
  strncpy((char *)wc.sta.password, pass, sizeof wc.sta.password - 1);
  /* WPA and up when there is a password; an open network only when the
   * provisioned password is empty, so a typo cannot silently join an
   * unsecured twin of the real AP. */
  wc.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_LOGI(TAG, "wifi connecting to '%s'", ssid);
}

bool hotpush_net_connected(void) { return s_connected; }

const char *hotpush_net_ip(void) { return s_ip; }
