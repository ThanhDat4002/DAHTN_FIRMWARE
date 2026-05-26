
#pragma once

#include <stdbool.h>
#include "types.h"

void sensor_init(void);


void sensor_task(void *pvParameters);

void sensor_wakeup_now(void);

bool sensor_read_once(pzem_data_t *data);
