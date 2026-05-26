/*
 * 1. Bảo vệ quá dòng (OVER_CURRENT): current > MAX_CURRENT_A → FAULT <100ms
 * 2. Bảo vệ quá áp (OVER_VOLTAGE): voltage > MAX_VOLTAGE_V → FAULT
 * 3. Bảo vệ thấp áp (UNDER_VOLTAGE): voltage < MIN_VOLTAGE_V → FAULT
 * 4. Tự ngắt khi sạc đầy (FULL_BATTERY): ZERO_CURRENT_EPS_A < I <= TRICKLE_CURRENT_A liên tục AUTO_CUTOFF_MS
 * 5. Chống trộm điện (UNPLUGGED): I <= ZERO_CURRENT_EPS_A liên tục UNPLUG_DETECT_MS → ngắt relay
 * 6. Lỗi cảm biến (SENSOR_ERROR): mất kết nối PZEM → FAULT
 * 7. Relay Fault: ra lệnh OFF nhưng vẫn có dòng → khóa khẩn cấp
*/

#pragma once

void safety_task(void *pvParameters);
