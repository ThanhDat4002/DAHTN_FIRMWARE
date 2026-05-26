#pragma once

#include <stdbool.h>


// wifi.c -> wifi_portal.c
void wifi_internal_prepare_for_captive_portal(void);
void wifi_internal_on_portal_config_saved(void);

// wifi_portal.c -> wifi.c
bool wifi_portal_is_active(void);
void wifi_portal_start(void);
