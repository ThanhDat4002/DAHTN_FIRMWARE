#include "dns_server.h"

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/err.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#define DNS_PORT 53
#define DNS_MAX_LEN 256
#define OPCODE_MASK 0x7800
#define QR_FLAG (1 << 7)
#define QD_TYPE_A 0x0001
#define ANS_TTL_SEC 300

static const char *TAG = "DNS";

/*
 * DNS captive portal helper
 * Muc tieu: tra loi A record cho moi query ve IP cua AP.
 * Nho do client mo domain bat ky se bi redirect ve web portal.
 */
typedef struct __attribute__((__packed__)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qd_count;
    uint16_t an_count;
    uint16_t ns_count;
    uint16_t ar_count;
} dns_header_t;
typedef struct __attribute__((__packed__)) {
    uint16_t type;
    uint16_t class;
} dns_question_t;
typedef struct __attribute__((__packed__)) {
    uint16_t ptr_offset;
    uint16_t type;
    uint16_t class;
    uint32_t ttl;
    uint16_t addr_len;
    uint32_t ip_addr;
} dns_answer_t;

struct dns_server_handle {
    bool started;
    TaskHandle_t task;
    int sock;
    int num_of_entries;
    dns_entry_pair_t entry[];
};

static const uint8_t *parse_dns_name(const uint8_t *packet_start,
                                     size_t packet_len,
                                     const uint8_t *raw_name,
                                     char *parsed_name,
                                     size_t parsed_name_max_len) {
    size_t out_pos = 0;
    const uint8_t *label = raw_name;
    const uint8_t *packet_end = packet_start + packet_len;

    if (packet_start == NULL || raw_name == NULL || parsed_name == NULL ||
        parsed_name_max_len == 0 || raw_name < packet_start || raw_name >= packet_end) {
        return NULL;
    }

    // Parse ten mien dang DNS label format: [len][label]...[0]
    while (label < packet_end) {
        uint8_t sub_name_len = *label;
        if (sub_name_len == 0U) {
            if (out_pos == 0U) {
                return NULL;
            }
            parsed_name[out_pos - 1U] = '\0';
            return label + 1;
        }

        if ((sub_name_len & 0xC0U) != 0U || sub_name_len > 63U) {
            return NULL;
        }

        if ((size_t)(packet_end - label) < (size_t)sub_name_len + 1U) {
            return NULL;
        }

        if (out_pos + (size_t)sub_name_len + 1U >= parsed_name_max_len) {
            return NULL;
        }

        memcpy(parsed_name + out_pos, label + 1, sub_name_len);
        out_pos += sub_name_len;
        parsed_name[out_pos++] = '.';
        label += (size_t)sub_name_len + 1U;
    }

    return NULL;
}
// Phân tích dữ liệu trong parse_dns_request.
static int parse_dns_request(char *req,
                             size_t req_len,
                             char *dns_reply,
                             size_t dns_reply_max_len,
                             dns_server_handle_t handle) {
    uint8_t *cur_ans_ptr;
    uint8_t *cur_qd_ptr;
    const uint8_t *req_bytes = (const uint8_t *)req;
    const uint8_t *req_end = req_bytes + req_len;
    dns_header_t *header;
    uint16_t qd_count;
    uint16_t an_count = 0;
    int reply_len;
    char name[128];

    if (req == NULL || dns_reply == NULL || handle == NULL ||
        req_len < sizeof(dns_header_t) || req_len > dns_reply_max_len) {
        return -1;
    }

    // Reply duoc build tren ban copy request de giu nguyen question section.
    memset(dns_reply, 0, dns_reply_max_len);
    memcpy(dns_reply, req, req_len);

    header = (dns_header_t *)dns_reply;
    if ((header->flags & OPCODE_MASK) != 0) {
        return 0;
    }

    header->flags |= QR_FLAG;
    qd_count = ntohs(header->qd_count);
    if (qd_count == 0U ||
        qd_count > (uint16_t)((dns_reply_max_len - req_len) / sizeof(dns_answer_t))) {
        return -1;
    }

    reply_len = (int)(req_len + (qd_count * sizeof(dns_answer_t)));
    if ((size_t)reply_len > dns_reply_max_len) {
        return -1;
    }

    cur_ans_ptr = (uint8_t *)dns_reply + req_len;
    cur_qd_ptr = (uint8_t *)dns_reply + sizeof(dns_header_t);

    for (int qd_i = 0; qd_i < qd_count; qd_i++) {
        if ((const uint8_t *)cur_qd_ptr < (const uint8_t *)dns_reply + sizeof(dns_header_t) ||
            (const uint8_t *)cur_qd_ptr >= (const uint8_t *)dns_reply + req_len) {
            return -1;
        }

        size_t qd_offset = (size_t)((const uint8_t *)cur_qd_ptr - (const uint8_t *)dns_reply);
        const uint8_t *req_qd_ptr = req_bytes + qd_offset;
        if (req_qd_ptr >= req_end) {
            return -1;
        }

        const uint8_t *name_end_ptr =
            parse_dns_name(req_bytes, req_len, req_qd_ptr, name, sizeof(name));
        if (name_end_ptr == NULL) {
            ESP_LOGE(TAG, "Khong phan tich duoc goi hoi DNS.");
            return -1;
        }

        if ((size_t)(req_end - name_end_ptr) < sizeof(dns_question_t)) {
            return -1;
        }
        const dns_question_t *question = (const dns_question_t *)name_end_ptr;
        uint16_t qd_type = ntohs(question->type);
        uint16_t qd_class = ntohs(question->class);

        if (qd_type == QD_TYPE_A) {
            esp_ip4_addr_t ip = { .addr = IPADDR_ANY };

            // Tim entry hop le: wildcard "*" hoac domain cu the.
            for (int i = 0; i < handle->num_of_entries; ++i) {
                if (strcmp(handle->entry[i].name, "*") == 0 ||
                    strcmp(handle->entry[i].name, name) == 0) {
                    if (handle->entry[i].if_key != NULL) {
                        esp_netif_ip_info_t ip_info;
                        esp_netif_t *netif = esp_netif_get_handle_from_ifkey(handle->entry[i].if_key);
                        if (netif == NULL) {
                            break;
                        }
                        esp_netif_get_ip_info(netif, &ip_info);
                        ip.addr = ip_info.ip.addr;
                        break;
                    } else if (handle->entry[i].ip.addr != IPADDR_ANY) {
                        ip.addr = handle->entry[i].ip.addr;
                        break;
                    }
                }
            }

            if (ip.addr == IPADDR_ANY) {
                cur_qd_ptr = (uint8_t *)cur_qd_ptr +
                             (size_t)(name_end_ptr - req_qd_ptr) +
                             sizeof(dns_question_t);
                continue;
            }

            // Dung pointer compression (0xC000 | offset) de tro lai question name.
            dns_answer_t *answer = (dns_answer_t *)cur_ans_ptr;
            answer->ptr_offset = htons((uint16_t)(0xC000 | qd_offset));
            answer->type = htons(qd_type);
            answer->class = htons(qd_class);
            answer->ttl = htonl(ANS_TTL_SEC);
            answer->addr_len = htons(sizeof(ip.addr));
            answer->ip_addr = ip.addr;
            cur_ans_ptr += (size_t)sizeof(dns_answer_t);
            an_count++;
        }

        cur_qd_ptr = (uint8_t *)cur_qd_ptr + (size_t)(name_end_ptr - req_qd_ptr) + sizeof(dns_question_t);
    }

    header->an_count = htons(an_count);
    reply_len = (int)(req_len + ((int)an_count * (int)sizeof(dns_answer_t)));
    return reply_len;
}
// Chạy tác vụ trong dns_server_task.
static void dns_server_task(void *pvParameters) {
    dns_server_handle_t handle = (dns_server_handle_t)pvParameters;
    char rx_buffer[128];
    char addr_str[128];
    int sock = -1;

    // Outer loop: tao lai socket neu co loi runtime.
    while (handle->started) {
        struct sockaddr_in dest_addr = {
            .sin_addr.s_addr = htonl(INADDR_ANY),
            .sin_family = AF_INET,
            .sin_port = htons(DNS_PORT),
        };

        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (sock < 0) {
            ESP_LOGE(TAG, "Khong tao duoc socket: errno=%d", errno);
            break;
        }
        handle->sock = sock;

        if (bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0) {
            ESP_LOGE(TAG, "Socket bind that bai: errno=%d", errno);
            close(sock);
            handle->sock = -1;
            break;
        }

        ESP_LOGI(TAG, "DNS server dang lang nghe o cong %d", DNS_PORT);

        // Inner loop: nhan query va tra loi ngay tren UDP 53.
        while (handle->started) {
            struct sockaddr_in6 source_addr;
            socklen_t socklen = sizeof(source_addr);
            int len = recvfrom(sock,
                               rx_buffer,
                               sizeof(rx_buffer) - 1,
                               0,
                               (struct sockaddr *)&source_addr,
                               &socklen);

            if (len < 0) {
                if (handle->started) {
                    ESP_LOGE(TAG, "recvfrom that bai: errno=%d", errno);
                }
                close(sock);
                handle->sock = -1;
                sock = -1;
                break;
            }

            if (source_addr.sin6_family == PF_INET) {
                inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr.s_addr,
                            addr_str,
                            sizeof(addr_str) - 1);
            } else {
                inet6_ntoa_r(source_addr.sin6_addr, addr_str, sizeof(addr_str) - 1);
            }

            rx_buffer[len] = 0;

            char reply[DNS_MAX_LEN];
            int reply_len = parse_dns_request(rx_buffer, (size_t)len, reply, sizeof(reply), handle);
            if (reply_len <= 0) {
                continue;
            }

            if (sendto(sock,
                       reply,
                       reply_len,
                       0,
                       (struct sockaddr *)&source_addr,
                       socklen) < 0) {
                if (handle->started) {
                    ESP_LOGE(TAG, "sendto that bai: errno=%d", errno);
                }
                close(sock);
                handle->sock = -1;
                sock = -1;
                break;
            }
        }

        if (sock != -1) {
            shutdown(sock, 0);
            close(sock);
            handle->sock = -1;
        }
    }

    handle->task = NULL;
    vTaskDelete(NULL);
}
// Bắt đầu tiến trình trong start_dns_server.
dns_server_handle_t start_dns_server(const dns_server_config_t *config) {
    if (config == NULL || config->num_of_entries <= 0) {
        return NULL;
    }

    // Cap phat 1 khoi nho gom handle + mang entry variable-length.
    dns_server_handle_t handle = calloc(
        1,
        sizeof(struct dns_server_handle) + ((size_t)config->num_of_entries * sizeof(dns_entry_pair_t)));
    if (handle == NULL) {
        ESP_LOGE(TAG, "Khong cap phat duoc handle DNS server.");
        return NULL;
    }

    handle->started = true;
    handle->sock = -1;
    handle->num_of_entries = config->num_of_entries;
    memcpy(handle->entry, config->item, (size_t)config->num_of_entries * sizeof(dns_entry_pair_t));

    if (xTaskCreate(dns_server_task, "dns_server", 4096, handle, 5, &handle->task) != pdPASS) {
        free(handle);
        ESP_LOGE(TAG, "Khong tao duoc tac vu DNS server.");
        return NULL;
    }

    return handle;
}
// Dừng tiến trình trong stop_dns_server.
void stop_dns_server(dns_server_handle_t handle) {
    if (handle == NULL) {
        return;
    }

    handle->started = false;

    // Dong socket de recvfrom thoat, task tu dong ket thuc.
    if (handle->sock >= 0) {
        shutdown(handle->sock, 0);
        close(handle->sock);
        handle->sock = -1;
    }

    for (int i = 0; i < 50 && handle->task != NULL; ++i) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    if (handle->task != NULL) {
        vTaskDelete(handle->task);
        handle->task = NULL;
    }

    free(handle);
}


