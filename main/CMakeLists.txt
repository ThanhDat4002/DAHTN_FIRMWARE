idf_component_register(
    SRCS
        "main.c"
        "globals.c"
        "globals_session.c"
        "nvs_manager.c"
        "relay.c"
       
        "sensor.c"
        "ui.c"
       
        "../wifi/wifi.c"
        "../wifi/wifi_portal.c"
        "../wifi/dns_server.c"
        "safety.c"
       
        "../cloud/cloud.c"
        "../cloud/cloud_http.c"
        "../cloud/cloud_config.c"
        "../cloud/cloud_command.c"
        "../cloud/cloud_report.c"
        "ota.c"
        "ota_firebase.c"
       
       

    INCLUDE_DIRS "." "../wifi" "../cloud"
    REQUIRES 
        driver 
        esp_event 
        esp_http_client 
        esp_http_server
        esp_https_ota
        esp_netif 
        esp_timer 
        esp_wifi 
        nvs_flash
        json
        app_update
        mbedtls
)
