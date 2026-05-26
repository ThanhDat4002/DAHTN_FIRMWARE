/** 
  LED patterns theo từng trạng thái hệ thống:
   - IDLE/OFFLINE : LED1 nhấp nháy chậm (1s ON / 1s OFF)
   - READY        : LED1 sáng tĩnh (Solid On)
   - CHARGING     : LED2 nháy nhanh (200ms ON / 200ms OFF)
   - FAULT        : LED3 nháy kép  (2 lần chớp nhanh → nghỉ 1s)
 
  Buzzer patterns:
    - 1 beep ngắn  : Bắt đầu sạc thành công
    - 2 beep       : Dừng sạc hoặc WiFi setup thành công
    - 3 beep dài   : Sự cố / quá dòng
 */

#pragma once

#include <stdint.h>

void ui_init(void);

void ui_task(void *pvParameters);
void beep(int count, int duration_ms);
