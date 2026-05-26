/**
 * @file wifi_portal.c
 * @brief Captive Portal: AP mode + HTTP form + DNS hijack Ä‘á»ƒ user cáº¥u hÃ¬nh WiFi.
 *
 * ToÃ n bá»™ tÃ i nguyÃªn web server (httpd, DNS, HTML, handlers) Ä‘Æ°á»£c Ä‘Ã³ng gÃ³i trong
 * file nÃ y. wifi.c chá»‰ tÆ°Æ¡ng tÃ¡c qua 2 hÃ m:
 *  - `wifi_portal_start()`  Ä‘á»ƒ báº­t portal
 *  - `wifi_portal_is_active()` Ä‘á»ƒ kiá»ƒm tra tráº¡ng thÃ¡i
 */

#include "wifi_internal.h"
#include "config.h"
#include "dns_server.h"
#include "nvs_manager.h"
#include "types.h"
#include "ui.h"

#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#define TAG "WIFI_PORTAL"

// EMBEDDED HTML
static const char PORTAL_HTML[] =
    "<!DOCTYPE html><html lang='en'><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>EVION Setup</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:Arial,sans-serif;background:#0f172a;color:#e2e8f0;display:flex;justify-content:center;align-items:center;min-height:100vh;padding:20px}"
    ".card{background:#1e293b;border-radius:16px;padding:32px;width:100%;max-width:420px;box-shadow:0 8px 32px rgba(0,0,0,.45)}"
    "h1{text-align:center;color:#38bdf8;font-size:1.5rem;margin-bottom:8px}"
    "p.sub{text-align:center;color:#94a3b8;font-size:.9rem;margin-bottom:24px;line-height:1.5}"
    "label{display:block;font-size:.82rem;color:#94a3b8;margin:16px 0 6px}"
    "input{width:100%;padding:10px 14px;background:#0f172a;border:1px solid #334155;border-radius:8px;color:#e2e8f0;font-size:1rem;outline:none}"
    "input:focus{border-color:#38bdf8}"
    ".section-title{color:#38bdf8;font-size:.75rem;font-weight:700;text-transform:uppercase;letter-spacing:.05em;margin-top:20px;margin-bottom:4px;border-bottom:1px solid #334155;padding-bottom:4px}"
    "button{width:100%;margin-top:24px;padding:12px;background:#38bdf8;color:#0f172a;border:none;border-radius:8px;font-size:1rem;font-weight:700;cursor:pointer}"
    "button:hover{background:#7dd3fc}"
    ".note{margin-top:16px;font-size:.78rem;color:#94a3b8;text-align:center;line-height:1.5}"
    "</style></head><body>"
    "<div class='card'>"
    "<h1>EVION Setup</h1>"
    "<p class='sub'>Nháº­p WiFi Ä‘á»ƒ tráº¡m káº¿t ná»‘i máº¡ng .</p>"
    "<form action='/save' method='POST'>"
    "<div class='section-title'>WiFi</div>"
    "<label>WiFi SSID</label>"
    "<input type='text' name='ssid' placeholder='Nháº­p SSID' required maxlength='32'>"
    "<label>WiFi Password</label>"
    "<input type='password' name='password' placeholder='Nháº­p máº­t kháº©u' maxlength='64'>"
    "<div class='section-title'>Station</div>"
    "<label>Device ID</label>"
    "<input type='text' name='device_id' placeholder='station_01' maxlength='32'>"
    "<button type='submit'>LÆ°u vÃ  káº¿t ná»‘i</button>"
    "</form>"
    "<p class='note'>Náº¿u bá» trá»‘ng Device ID sáº½ dÃ¹ng máº·c Ä‘á»‹nh: " FIREBASE_DEVICE_ID "</p>"
    "</div></body></html>";

static const char SAVED_HTML[] =
    "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    "<meta http-equiv='refresh' content='5'>"
    "<title>Saved</title>"
    "<style>body{font-family:Arial;background:#0f172a;color:#e2e8f0;text-align:center;padding:60px}"
    "h1{color:#4ade80;font-size:2rem}p{color:#94a3b8;margin-top:16px;line-height:1.5}</style></head><body>"
    "<h1>Configuration saved</h1>"
    "<p>Device is restarting to connect to WiFi.</p>"
    "<p style='color:#38bdf8'>This page will close automatically in 5 seconds.</p>"
    "</body></html>";

// INTERNAL STATE
static httpd_handle_t      s_httpd                 = NULL;
static dns_server_handle_t s_dns_server            = NULL;
static volatile bool       s_captive_portal_active = false;

// FORM PARSING HELPERS
// Thực hiện xử lý trong url_decode.
static void url_decode(const char *src, char *dst, size_t maxlen) {
    size_t i = 0;
    size_t j = 0;

    while (src[i] != '\0' && j < maxlen - 1) {
        if (src[i] == '%' && src[i + 1] != '\0' && src[i + 2] != '\0') {
            char hex[3] = {src[i + 1], src[i + 2], '\0'};
            dst[j++] = (char)strtol(hex, NULL, 16);
            i += 3;
        } else if (src[i] == '+') {
            dst[j++] = ' ';
            i++;
        } else {
            dst[j++] = src[i++];
        }
    }

    dst[j] = '\0';
}
// Thực hiện xử lý trong extract_field.
static bool extract_field(const char *body, const char *key, char *out, size_t out_len) {
    char search[64];
    const char *p;
    const char *end;
    size_t len;
    char encoded[256] = {0};

    snprintf(search, sizeof(search), "%s=", key);
    p = strstr(body, search);
    if (p == NULL) {
        out[0] = '\0';
        return false;
    }

    p += strlen(search);
    end = strchr(p, '&');
    len = (end != NULL) ? (size_t)(end - p) : strlen(p);
    if (len >= sizeof(encoded)) {
        len = sizeof(encoded) - 1;
    }

    memcpy(encoded, p, len);
    url_decode(encoded, out, out_len);
    return true;
}

// HTTP HANDLERS
// Lấy dữ liệu trong portal_get_handler.
static esp_err_t portal_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_send(req, PORTAL_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}
// Lưu dữ liệu trong portal_save_handler.
static esp_err_t portal_save_handler(httpd_req_t *req) {
    char body[512] = {0};
    wifi_config_data_t pending_cfg = {0};
    size_t total_len = (size_t)req->content_len;
    size_t total_read = 0;

    if (total_len == 0 || total_len >= sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid form data length");
        return ESP_FAIL;
    }

    while (total_read < total_len) {
        int ret = httpd_req_recv(req, body + total_read, total_len - total_read);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (ret <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to read form data");
            return ESP_FAIL;
        }
        total_read += (size_t)ret;
    }

    body[total_read] = '\0';

    extract_field(body, "ssid",      pending_cfg.ssid,      sizeof(pending_cfg.ssid));
    extract_field(body, "password",  pending_cfg.password,  sizeof(pending_cfg.password));
    extract_field(body, "device_id", pending_cfg.device_id, sizeof(pending_cfg.device_id));

    if (strlen(pending_cfg.ssid) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID must not be empty");
        return ESP_FAIL;
    }

    if (pending_cfg.device_id[0] == '\0') {
        strncpy(pending_cfg.device_id, FIREBASE_DEVICE_ID, sizeof(pending_cfg.device_id) - 1);
        pending_cfg.device_id[sizeof(pending_cfg.device_id) - 1] = '\0';
    }

    if (!nvs_save_wifi_config(&pending_cfg)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save WiFi config");
        return ESP_FAIL;
    }
    if (!nvs_save_cloud_config(&pending_cfg)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save cloud config");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, SAVED_HTML, HTTPD_RESP_USE_STRLEN);

    wifi_internal_on_portal_config_saved();
    beep(2, 200);
    return ESP_OK;
}
// Xử lý sự kiện trong portal_redirect_handler.
static esp_err_t portal_redirect_handler(httpd_req_t *req, httpd_err_code_t err) {
    (void)err;
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, "Redirecting to setup portal", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// DHCP CAPTIVE PORTAL OPTION
// Thực hiện xử lý trong configure_captive_portal_dhcp_option.
static void configure_captive_portal_dhcp_option(void) {
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    char captive_portal_uri[32];

    if (ap_netif == NULL) {
        ESP_LOGW(TAG, "Khong cau hinh duoc DHCP option cho captive portal: khong tim thay AP netif.");
        return;
    }

    snprintf(captive_portal_uri, sizeof(captive_portal_uri), "http://%s", WIFI_AP_IP);
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_stop(ap_netif));

    esp_err_t err = esp_netif_dhcps_option(ap_netif,
                                           ESP_NETIF_OP_SET,
                                           ESP_NETIF_CAPTIVEPORTAL_URI,
                                           captive_portal_uri,
                                           strlen(captive_portal_uri));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Khong dat duoc DHCP option URI captive portal: %s", esp_err_to_name(err));
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_start(ap_netif));
}

// PUBLIC API
// Thực hiện xử lý trong wifi_portal_is_active.
bool wifi_portal_is_active(void) {
    return s_captive_portal_active;
}
// Thực hiện xử lý trong wifi_portal_start.
void wifi_portal_start(void) {
    if (s_captive_portal_active) {
        return;
    }

    ESP_LOGI(TAG, "Bat dau AP che do Captive Portal: %s", WIFI_AP_SSID);

    wifi_internal_prepare_for_captive_portal();

    wifi_config_t ap_cfg = {
        .ap = {
            .channel = WIFI_AP_CHANNEL,
            .max_connection = WIFI_AP_MAX_CONN,
            .authmode = (strlen(WIFI_AP_PASSWORD) == 0) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char *)ap_cfg.ap.ssid, WIFI_AP_SSID, sizeof(ap_cfg.ap.ssid) - 1);
    strncpy((char *)ap_cfg.ap.password, WIFI_AP_PASSWORD, sizeof(ap_cfg.ap.password) - 1);
    ap_cfg.ap.ssid_len = strlen(WIFI_AP_SSID);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    configure_captive_portal_dhcp_option();

    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    http_cfg.max_uri_handlers = 8;
    http_cfg.lru_purge_enable = true;

    if (s_httpd != NULL) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }

    if (httpd_start(&s_httpd, &http_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Khong the khoi dong may chu HTTP cho che do Captive Portal.");
        return;
    }

    httpd_uri_t page_root = {.uri = "/",     .method = HTTP_GET,  .handler = portal_get_handler};
    httpd_uri_t page_save = {.uri = "/save", .method = HTTP_POST, .handler = portal_save_handler};
    httpd_register_uri_handler(s_httpd, &page_root);
    httpd_register_uri_handler(s_httpd, &page_save);
    httpd_register_err_handler(s_httpd, HTTPD_404_NOT_FOUND, portal_redirect_handler);

    if (s_dns_server != NULL) {
        stop_dns_server(s_dns_server);
        s_dns_server = NULL;
    }

    dns_server_config_t dns_cfg = DNS_SERVER_CONFIG_SINGLE("*", "WIFI_AP_DEF");
    s_dns_server = start_dns_server(&dns_cfg);
    if (s_dns_server == NULL) {
        ESP_LOGW(TAG, "DNS redirect server khong khoi dong duoc.");
    }

    s_captive_portal_active = true;
    ESP_LOGI(TAG, "Che do Captive Portal san sang tai http://%s", WIFI_AP_IP);
}


