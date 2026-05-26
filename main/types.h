/**
 * @file types.h
 * @brief nh ngha tt c kiu d liu dng chung trong h thng EVION.
 *
 * File ny cha cc enum v struct c chia s gia tt c module.
 * Khng c logic x l  y, ch c nh ngha kiu d liu.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

// TRNG THI H THNG (System State Machine)
/**
 * @brief Cc trng thi ca my trng thi h thng (FSM).
 *
 * Chuyn i trng thi hp l:
 *   BOOT  IDLE/OFFLINE  READY  CHARGING  STOPPED  READY
 *                                            FAULT  READY (sau reset)
 */
typedef enum {
    STATE_BOOT      = 0,  /**< ang khi ng, cha init xong */
    STATE_IDLE      = 1,  /**< C in, cha c WiFi */
    STATE_OFFLINE   = 2,  /**< ang sc nhng mt WiFi (tip tc sc) */
    STATE_READY     = 3,  /**<  kt ni WiFi + Firebase, ch lnh sc */
    STATE_CHARGING  = 4,  /**< Relay ng, ang sc cho xe */
    STATE_STOPPED   = 5,  /**< Phin sc kt thc, ch ngi dng rt sc */
    STATE_FAULT     = 6,  /**< Li phn cng, relay ngt khn cp */
} system_state_t;

// M LI H THNG (Error Codes)
/**
 * @brief M li  bo co ln Firebase v hin th trn UI.
 */
typedef enum {
    ERR_NONE            = 0,  /**< Hot ng bnh thng */
    ERR_OVER_CURRENT    = 1,  /**< Dng in vt ngng MAX_CURRENT_A */
    ERR_OVER_VOLTAGE    = 2,  /**< in p vt ngng MAX_VOLTAGE_V */
    ERR_UNDER_VOLTAGE   = 3,  /**< in p thp hn MIN_VOLTAGE_V */
    ERR_SENSOR_ERROR    = 4,  /**< Mt kt ni UART vi PZEM-004T */
    ERR_RELAY_FAULT     = 5,  /**< Ra lnh ngt relay nhng vn c dng */
    ERR_FULL_BATTERY    = 6,  /**< Nhn din pin sc y (dng trickle 5 pht) */
    ERR_UNPLUGGED       = 7,  /**< Phch cm b rt t ngt */
} error_code_t;

/**
 * @brief Chuyn error_code_t sang chui  gi ln Firebase.
 * @param code M li cn chuyn i
 * @return Con tr n chui m t li (string literal, khng cn free)
 */
static inline const char *error_code_to_str(error_code_t code) {
    switch (code) {
        case ERR_NONE:           return "NONE";
        case ERR_OVER_CURRENT:   return "overload";
        case ERR_OVER_VOLTAGE:   return "OVER_VOLTAGE";
        case ERR_UNDER_VOLTAGE:  return "UNDER_VOLTAGE";
        case ERR_SENSOR_ERROR:   return "station_fault";
        case ERR_RELAY_FAULT:    return "RELAY_FAULT";
        case ERR_FULL_BATTERY:   return "full_charge";
        case ERR_UNPLUGGED:      return "unplugged";
        default:                 return "UNKNOWN";
    }
}

// L DO DNG SC (Stop Reasons)
/**
 * @brief L do kt thc phin sc, dng cho trng "end_reason" trong Firebase.
 */
typedef enum {
    STOP_REASON_NONE            = 0,  /**< Cha dng / gi tr khi to */
    STOP_REASON_USER_STOP       = 1,  /**< Lnh STOP_CHARGING t Firebase */
    STOP_REASON_FULL_CHARGE     = 2,  /**< T ng ngt khi sc y */
    STOP_REASON_OVERLOAD        = 3,  /**< Ngt do qu dng / qu p */
    STOP_REASON_UNPLUGGED       = 4,  /**< B rt phch cm */
    STOP_REASON_STATION_FAULT   = 5,  /**< Li phn cng trm */
    STOP_REASON_COMPLETED       = 6,  /**< Phin kt thc bnh thng */
    STOP_REASON_CONNECTION_ERROR = 7, /**< Li kt ni Firebase */
} stop_reason_t;

/**
 * @brief Chuyn stop_reason_t sang chui  gi ln Firebase.
 * @param reason L do dng sc
 * @return Con tr n chui m t (string literal)
 */
static inline const char *stop_reason_to_str(stop_reason_t reason) {
    switch (reason) {
        case STOP_REASON_NONE:             return "NONE";
        case STOP_REASON_USER_STOP:        return "user_stop";
        case STOP_REASON_FULL_CHARGE:      return "full_charge";
        case STOP_REASON_OVERLOAD:         return "overload";
        case STOP_REASON_UNPLUGGED:        return "unplugged";
        case STOP_REASON_STATION_FAULT:    return "station_fault";
        case STOP_REASON_COMPLETED:        return "completed";
        case STOP_REASON_CONNECTION_ERROR: return "connection_error";
        default:                           return "UNKNOWN";
    }
}

// TRNG THI KT QU SC
/**
 * @brief Kt qu tng th ca phin sc  hin th trn dashboard.
 */
typedef enum {
    CHARGE_RESULT_SUCCESS   = 0,  /**< Phin sc thnh cng */
    CHARGE_RESULT_WARNING   = 1,  /**< Kt thc c cnh bo */
    CHARGE_RESULT_ERROR     = 2,  /**< Kt thc do li */
} charge_result_status_t;

// LNH T FIREBASE (Commands)
/**
 * @brief Cc lnh iu khin t xa qua Firebase.
 *
 * c c t trng "command" trong Firebase Realtime Database.
 * Sau khi thc thi, ESP32 reset v CMD_NONE  trnh lp li.
 */
typedef enum {
    CMD_NONE            = 0,  /**< Khng c lnh ang ch */
    CMD_START_CHARGING  = 1,  /**< Bt u sc */
    CMD_STOP_CHARGING   = 2,  /**< Dng sc */
    CMD_RESET_DEVICE    = 3,  /**< Khi ng li ESP32 */
    CMD_UPDATE_FIRMWARE = 4,  /**< Cp nht firmware OTA */
    CMD_UPDATE_CONFIG   = 5,  /**< Cp nht cau hinh runtime (co the kem maxCurrent) */
    CMD_RESET_STATION   = 6,  /**< Alias reset trm */
    CMD_RELAY_ON        = 7,  /**< Alias legacy, c x l nh START_CHARGING */
    CMD_RELAY_OFF       = 8,  /**< Alias legacy, c x l nh STOP_CHARGING */
    CMD_INVALID         = 9,  /**< Lnh khng hp l / khng nhn ra */
} firebase_command_t;

/**
 * @brief Cu trc cha lnh iu khin nhn t Firebase.
 */
typedef struct {
    firebase_command_t  command;          /**< Loi lnh */
    char                command_id[MAX_COMMAND_ID_LEN]; /**< ID lnh trn RTDB */
    char                issued_by[MAX_ISSUED_BY_LEN];    /**< UID/ngi to lnh */
    char                user_id[64];      /**< ID ngi dng hng phin sc */
    char                session_id[MAX_SESSION_ID_LEN]; /**< Session ID c cp */
    int64_t             timestamp;        /**< Thi im pht lnh (Unix seconds) */
    int64_t             expire_at;        /**< Thi im ht hn lnh */
    float               max_current;      /**< Gioi han dong dung cho session (uu tien payload, fallback runtime) */
    int                 target_energy_wh;  /**< Mc tiu nng lng Wh */
    float    target_energy_kwh;            /**< Target energy for auto-stop (kWh). */
    int                 price_per_kwh;     /**< Gi in / kWh */
    bool                relay;            /**< Trng thi relay yu cu */
} station_command_t;

// D LIU CM BIN PZEM-004T
/**
 * @brief D liu in nng c t cm bin PZEM-004T mi 500ms.
 */
typedef struct {
    float    voltage;       /**< in p li AC (V) */
    float    current;       /**< Dng in ti (A) */
    float    power;         /**< Cng sut tc thi (W) */
    float    energy_kwh;    /**< Tng in nng tch ly trong PZEM (kWh) */
    float    frequency;     /**< Tn s li in (Hz) */
    float    power_factor;  /**< H s cng sut */
    bool     valid;         /**< TRUE nu c thnh cng, FALSE nu SENSOR_ERROR */
    uint32_t timestamp_ms;  /**< Thi im c c (ms tnh t boot) */
} pzem_data_t;

// PHIN SC (Charging Session)
/**
 * @brief Thng tin phin sc hin ti.
 *
 * c lu nh k vo NVS Flash mi NVS_SESSION_SAVE_PERIOD_MS  c th
 * phc hi sau mt in t ngt.
 */
typedef struct {
    bool     active;                        /**< TRUE nu phin ang din ra */
    char     session_id[MAX_SESSION_ID_LEN]; /**< ID phin sc duy nht */
    char     user_id[64];                   /**< ID ngi dng */
    int64_t  start_time;                    /**< Unix timestamp lc bt u (s) */
    int64_t  end_time;                      /**< Unix timestamp lc kt thc (s) */
    float    energy_start_kwh;              /**< S kWh ng h PZEM lc bt u */
    float    energy_used_kwh;              /**< in tiu th trong phin ny (kWh) */
    stop_reason_t end_reason;              /**< L do kt thc */
    float    max_current_limit;            /**< Gii hn dng c p dng cho phin */
    float    target_energy_kwh;            /**< Target energy for auto-stop (kWh). */
} charging_session_t;

// CU HNH WIFI / CLOUD
/**
 * @brief Struct dng chung cho WiFi credentials v Firebase runtime config.
 */
typedef struct {
    char ssid[MAX_SSID_LEN];          /**< SSID ca mng WiFi */
    char password[MAX_PASSWORD_LEN];  /**< Mt khu WiFi */
    char firebase_url[MAX_URL_LEN];   /**< URL Firebase Realtime Database */
    char firebase_token[MAX_TOKEN_LEN]; /**< Optional RTDB REST auth token; not Web API key/VAPID key */
    char device_id[MAX_DEVICE_ID_LEN]; /**< ID thit b (station_01, ...) */
} wifi_config_data_t;

// TRNG THI KT NI MNG
/**
 * @brief Trng thi kt ni WiFi/Firebase ca h thng.
 */
typedef enum {
    NET_DISCONNECTED    = 0,  /**< Khng c kt ni WiFi */
    NET_CONNECTING      = 1,  /**< ang kt ni WiFi */
    NET_WIFI_CONNECTED  = 2,  /**<  kt ni WiFi, cha xc nhn Firebase */
    NET_ONLINE          = 3,  /**<  kt ni WiFi v Firebase hot ng bnh thng */
    NET_CAPTIVE_PORTAL  = 4,  /**< ang chy AP + Captive Portal  cu hnh */
} network_status_t;
