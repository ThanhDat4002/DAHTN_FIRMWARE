# MÔ TẢ PHẦN MỀM HỆ THỐNG TRẠM SẠC EV — ESP32 FIRMWARE

## 1. TỔNG QUAN HỆ THỐNG

### 1.1. Giới thiệu

Hệ thống **EVION** là một nền tảng trạm sạc xe điện (EV) IoT thông minh, bao gồm hai thành phần chính:

- **Ứng dụng Web (Cloud):** React + TypeScript, sử dụng Firebase (Firestore + Realtime Database) làm backend. Cung cấp giao diện cho người dùng tìm trạm, nạp ví, bắt đầu/dừng sạc, xem lịch sử; và giao diện Admin để quản lý trạm, giám sát realtime, cập nhật OTA, hỗ trợ người dùng.
- **Firmware ESP32 (Embedded):** Phần mềm nhúng chạy trên vi điều khiển ESP32, điều khiển phần cứng trạm sạc (relay, cảm biến), giao tiếp cloud qua WiFi/Firebase, đảm bảo an toàn điện, và hỗ trợ cập nhật firmware từ xa (OTA).

Tài liệu này **mô tả chi tiết phần mềm ESP32** — bao gồm kiến trúc, giao thức giao tiếp với cloud, luồng hoạt động, và cấu trúc dữ liệu — được thiết kế để **khớp hoàn toàn** với hệ thống Web đã triển khai.

### 1.2. Nền tảng phần cứng

| Thành phần | Chi tiết |
|---|---|
| MCU | ESP32-WROOM-32 (Dual-Core Xtensa LX6, 240 MHz) |
| Framework | ESP-IDF v5.x + FreeRTOS |
| Kết nối | WiFi 802.11 b/g/n, BLE 4.2 |
| Cảm biến | PZEM-004T v3.0 (UART – đo V, I, P, E) |
| Đầu ra | Relay điều khiển sạc (GPIO) |
| Lưu trữ | NVS (Non-Volatile Storage) cho session recovery |

### 1.3. Sơ đồ kiến trúc tổng quan

```
┌──────────────────────────────────────────────────────────────────────┐
│                        EVION CLOUD (Firebase)                        │
│                                                                      │
│  ┌─────────────────┐  ┌──────────────────┐  ┌────────────────────┐  │
│  │   Firestore DB  │  │ Realtime Database │  │  Firebase Storage  │  │
│  │  (sessions,     │  │ (stations/{id}/   │  │  (ota/firmware_    │  │
│  │   stations,     │  │   status,         │  │   *.bin)           │  │
│  │   users, ...)   │  │   telemetry,      │  │                    │  │
│  │                 │  │   command,        │  │                    │  │
│  │                 │  │   heartbeat,      │  │                    │  │
│  │                 │  │   config)         │  │                    │  │
│  └────────┬────────┘  └────────┬─────────┘  └─────────┬──────────┘  │
│           │                    │                       │             │
│  ┌────────┴────────────────────┴───────────────────────┴──────────┐  │
│  │                     Express API Server                         │  │
│  │  /api/station/command   (Web → RTDB → ESP32)                  │  │
│  │  /api/station/report    (ESP32 → Server → Firestore)          │  │
│  │  /api/payment/webhook   (SePay → Server → Firestore)          │  │
│  └────────────────────────────────────────────────────────────────┘  │
│                                                                      │
│  ┌────────────────────────────────────────────────────────────────┐  │
│  │                        Web Frontend                            │  │
│  │  User: Stations, Packages, Charging, History, Settings        │  │
│  │  Admin: Dashboard, History, Support, OTA, Settings             │  │
│  └────────────────────────────────────────────────────────────────┘  │
└──────────────────────────┬───────────────────────────────────────────┘
                           │ WiFi (HTTPS)
                           │
┌──────────────────────────┴───────────────────────────────────────────┐
│                         ESP32 FIRMWARE                                │
│                                                                      │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌───────┐ ┌─────────────┐  │
│  │ WiFi/BLE │ │  RTDB    │ │ Safety   │ │ Relay │ │ NVS Session │  │
│  │ Manager  │ │ Client   │ │ Monitor  │ │ Ctrl  │ │ Recovery    │  │
│  └──────────┘ └──────────┘ └──────────┘ └───────┘ └─────────────┘  │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌───────────────────────┐  │
│  │ Heartbeat│ │ Telemetry│ │   OTA    │ │    State Machine      │  │
│  │   Task   │ │   Task   │ │  Update  │ │ (READY→CHARGING→STOP) │  │
│  └──────────┘ └──────────┘ └──────────┘ └───────────────────────┘  │
└──────────────────────────────────────────────────────────────────────┘
```

---

## 2. CẤU TRÚC DỮ LIỆU FIREBASE REALTIME DATABASE

ESP32 giao tiếp chính qua **Firebase Realtime Database (RTDB)** tại node `stations/{stationId}/`. Dưới đây là cấu trúc dữ liệu đầy đủ mà ESP32 đọc và ghi:

### 2.1. Node `stations/{stationId}/status`

**Hướng:** ESP32 → Cloud (ESP32 ghi, Web đọc)

```json
{
  "state": "READY",
  "relay": false
}
```

| Trường | Kiểu | Giá trị | Mô tả |
|--------|------|---------|-------|
| `state` | string | `READY`, `CHARGING`, `STOPPED`, `FAULT`, `OFFLINE` | Trạng thái hiện tại của trạm sạc |
| `relay` | boolean | `true` / `false` | Trạng thái relay vật lý (ON/OFF) |

**Quy tắc trạng thái:**
- `READY`: Trạm sẵn sàng, relay OFF, không có phiên sạc.
- `CHARGING`: Relay ON, đang cấp điện cho xe.
- `STOPPED`: Phiên sạc vừa kết thúc, relay OFF, đang gửi report.
- `FAULT`: Phát hiện lỗi (quá tải, quá áp, mất kết nối cảm biến...), relay OFF.
- `OFFLINE`: (Server tự suy luận khi mất heartbeat > 30s, ESP32 không ghi giá trị này).

### 2.2. Node `stations/{stationId}/telemetry`

**Hướng:** ESP32 → Cloud (ESP32 ghi định kỳ mỗi 2-5 giây)

```json
{
  "voltage": 220.5,
  "current": 8.32,
  "power": 1.834,
  "energy_total": 2.456,
  "temperature": 35
}
```

| Trường | Kiểu | Đơn vị | Mô tả |
|--------|------|--------|-------|
| `voltage` | float | V (Volt) | Điện áp đo từ PZEM-004T |
| `current` | float | A (Ampere) | Dòng điện đo từ PZEM-004T |
| `power` | float | kW (Kilowatt) | Công suất thực tế |
| `energy_total` | float | kWh | Tổng năng lượng tích lũy trong phiên sạc hiện tại |
| `temperature` | float | °C | Nhiệt độ thiết bị (nếu có cảm biến nhiệt) |

### 2.3. Node `stations/{stationId}/heartbeat`

**Hướng:** ESP32 → Cloud (ESP32 ghi mỗi 10 giây)

```json
{
  "last_seen": 1713524160
}
```

| Trường | Kiểu | Mô tả |
|--------|------|-------|
| `last_seen` | integer | Unix timestamp (giây) lần cuối ESP32 gửi heartbeat. Server sử dụng trường này để xác định trạm ONLINE/OFFLINE. Nếu `now - last_seen > 30s` → trạm được coi là OFFLINE. |

### 2.4. Node `stations/{stationId}/command`

**Hướng:** Cloud → ESP32 (Web/Server ghi, ESP32 đọc và xử lý)

```json
{
  "id": "cmd_1713524160123_abc123",
  "type": "START_CHARGING",
  "issued_by": "user_uid_123",
  "timestamp": 1713524160,
  "expire_at": 1713524190,
  "status": "PENDING",
  "config": {
    "sessionId": "abc123xyz",
    "targetEnergyWh": 2857,
    "targetEnergyKwh": 2.857,
    "pricePerKwh": 3500
  }
}
```

| Trường | Kiểu | Mô tả |
|--------|------|-------|
| `id` | string | ID duy nhất của lệnh, format: `cmd_{timestamp}_{random}` |
| `type` | string | Loại lệnh: `START_CHARGING`, `STOP_CHARGING`, `RESET_STATION`, `RELAY_ON`, `RELAY_OFF` |
| `issued_by` | string | UID Firebase Auth của người phát lệnh |
| `timestamp` | integer | Unix timestamp (giây) thời điểm tạo lệnh |
| `expire_at` | integer | Unix timestamp (giây) hết hạn lệnh (TTL = 30 giây) |
| `status` | string | Trạng thái lệnh: `PENDING` → `ACCEPTED` / `REJECTED` / `EXPIRED` |
| `config` | object | Cấu hình kèm theo lệnh (chi tiết bên dưới) |

**Các trường config theo loại lệnh:**

| Loại lệnh | Config | Mô tả |
|------------|--------|-------|
| `START_CHARGING` | `sessionId`, `targetEnergyWh`, `targetEnergyKwh`, `pricePerKwh` | Thông tin phiên sạc: session ID từ Firestore, mục tiêu năng lượng (Wh và kWh), giá điện |
| `STOP_CHARGING` | `sessionId` (optional) | Dừng phiên sạc hiện tại |
| `RELAY_ON` | `relay: true` | Bật relay thủ công (admin only) |
| `RELAY_OFF` | `relay: false` | Tắt relay thủ công (admin only) |
| `RESET_STATION` | (không có) | Reset trạm về trạng thái READY |

### 2.5. Node `stations/{stationId}/config`

**Hướng:** Cloud → ESP32 (Admin Dashboard ghi khi chỉnh sửa trạm)

```json
{
  "maxCurrent": 32,
  "updatedAt": 1713524160,
  "source": "admin_dashboard"
}
```

| Trường | Kiểu | Mô tả |
|--------|------|-------|
| `maxCurrent` | integer | Dòng điện tối đa cho phép (Ampere). ESP32 dùng để bảo vệ quá dòng |
| `updatedAt` | integer | Unix timestamp lần cập nhật cuối |
| `source` | string | Nguồn cập nhật (vd: `admin_dashboard`) |

---

## 3. GIAO THỨC API HTTP (ESP32 → SERVER)

### 3.1. Report Endpoint — `POST /api/station/report`

Khi ESP32 kết thúc phiên sạc (tự động hoặc do lệnh dừng), ESP32 gửi báo cáo năng lượng thực tế về server để server thực hiện thanh toán và hoàn tiền.

> **Ghi chú bảo mật:** Endpoint này hiện **không yêu cầu Bearer token / xác thực**. Đây là thiết kế có chủ đích — ESP32 không có Firebase Auth user, và sessionId đóng vai trò như một "proof of knowledge" (chỉ ESP32 nhận sessionId từ command mới có thể gửi report). Trong môi trường production, nên bổ sung thêm cơ chế xác thực thiết bị (API key hoặc device certificate).

**Request:**
```http
POST /api/station/report
Content-Type: application/json

{
  "stationId": "ESP32-001",
  "sessionId": "abc123xyz",
  "energyWh": 2456.7,
  "status": "completed",
  "stopReason": "completed"
}
```

| Trường | Kiểu | Bắt buộc | Mô tả |
|--------|------|----------|-------|
| `stationId` | string | Có | ID trạm sạc (trùng với node RTDB) |
| `sessionId` | string | Có | Session ID nhận từ lệnh `START_CHARGING` |
| `energyWh` | float | Có | Tổng năng lượng đã sạc (Wh), đo từ PZEM-004T |
| `status` | string | Không | Trạng thái kết thúc: `completed`, `stopped`, `full_charge`, `overload`, `station_fault` |
| `stopReason` | string | Không | Lý do dừng chi tiết |
| `faultReason` | string | Không | Lý do lỗi (nếu có) |

**Response (200 OK):**
```json
{
  "success": true,
  "chargedAmount": 8600,
  "refundAmount": 1400,
  "prepaidAmount": 10000,
  "pricePerKwh": 3500,
  "targetEnergyWh": 2857,
  "alreadyProcessed": false
}
```

**Luồng xử lý phía Server khi nhận report (Settlement 2 bước):**

Server sử dụng cơ chế thanh toán 2 bước (two-phase settlement):

- **Bước 1 — `pending_report`:** Khi Server phát hiện trạng thái RTDB rời `CHARGING` (qua realtime listener), Server ngay lập tức đánh dấu session Firestore thành `{ status: "Stopped", settlementStatus: "pending_report" }`. Lúc này UI hiển thị phiên đã dừng nhưng chưa quyết toán.
- **Bước 2 — `settled`:** Khi ESP32 gửi report HTTP thành công, Server thực hiện quyết toán:
  1. Tìm session trong Firestore theo `sessionId`.
  2. Kiểm tra `settlementStatus` — chỉ xử lý nếu `"pending_report"` hoặc `status == "Charging"`.
  3. Tính tiền thực tế: `usedAmount = min(prepaidAmount, energyWh / 1000 * pricePerKwh)`.
  4. Tính tiền hoàn: `refundAmount = walletDebitedAmount - usedAmount`.
  5. Hoàn tiền dư vào ví người dùng (Firestore transaction).
  6. Cập nhật session: `settlementStatus: "settled"`, `energyWh`, `totalMoney`, `refundedAmount`, `stopReason`.
  7. Gửi push notification cho user và admin.

> **Quan trọng cho ESP32:** Nếu report HTTP bị lỗi, session sẽ mắc kẹt ở `pending_report`. Server có cơ chế backup: chạy `reconcileStuckChargingSessions()` mỗi **60 giây** để phát hiện và xử lý session bị kẹt. Tuy nhiên, ESP32 vẫn **nên retry** report để đảm bảo số liệu năng lượng chính xác.

### 3.2. Chiến lược Retry cho HTTP Report

Nếu `POST /api/station/report` thất bại (network error, HTTP 5xx, timeout), ESP32 phải retry:

```
Khi gửi report thất bại:
  1. State giữ ở STOPPED (KHÔNG chuyển READY)
  2. Retry tối đa 5 lần với exponential backoff:
     - Lần 1: chờ 2 giây
     - Lần 2: chờ 4 giây
     - Lần 3: chờ 8 giây
     - Lần 4: chờ 16 giây
     - Lần 5: chờ 32 giây
  3. Nếu cả 5 lần đều thất bại:
     a. Lưu report data vào NVS (sessionId, energyWh, stopReason)
     b. Chuyển state → READY (cho phép sạc mới)
     c. Khi WiFi kết nối lại → đọc NVS → gửi lại report pending
  4. Nếu response có alreadyProcessed == true:
     → Report đã được server xử lý trước đó (qua reconcile)
     → Bỏ qua, chuyển READY bình thường
```

### 3.2. Các giá trị `stopReason` hợp lệ

| Giá trị | Mô tả | Khi nào ESP32 gửi |
|---------|-------|---------------------|
| `completed` | Sạc đủ năng lượng mục tiêu `targetEnergyWh` | `energy_total >= targetEnergyWh` |
| `full_charge` | Pin xe đã đầy (dòng sạc giảm xuống mức tối thiểu) | Gói `10k` (sạc đầy) + dòng < ngưỡng |
| `user_stop` | Người dùng bấm dừng trên app | Nhận lệnh `STOP_CHARGING` |
| `overload` | Quá tải dòng/công suất | `current > maxCurrent` hoặc `power > maxPower` |
| `station_fault` | Lỗi phần cứng | Mất kết nối PZEM, relay lỗi... |
| `connection_error` | Mất kết nối WiFi/Cloud | WiFi disconnect quá lâu trong khi đang sạc |

---

## 4. MÁY TRẠNG THÁI (STATE MACHINE)

### 4.1. Sơ đồ trạng thái

```
                    ┌──────────┐
          ┌────────→│  READY   │←────────────┐
          │         └────┬─────┘             │
          │              │                    │
          │   START_CHARGING cmd              │ Report sent
          │              │                    │ + RESET
          │              ▼                    │
          │         ┌──────────┐             │
          │         │ CHARGING │─────────────│
          │         └────┬─────┘             │
          │              │                    │
          │    Target reached /               │
          │    STOP cmd /                     │
          │    Safety trigger                 │
          │              │                    │
          │              ▼                    │
          │         ┌──────────┐             │
          │         │ STOPPED  │─────────────┘
          │         └──────────┘
          │
          │         ┌──────────┐
          └─────────│  FAULT   │
                    └──────────┘
                      ↑
                      │ Safety violation
                      │ (from READY, CHARGING)
```

### 4.2. Bảng chuyển trạng thái

| Trạng thái hiện tại | Sự kiện | Trạng thái mới | Hành động |
|---------------------|---------|-----------------|-----------|
| READY | Nhận lệnh `START_CHARGING` | CHARGING | Bật relay, lưu session vào NVS, bắt đầu đo |
| CHARGING | `energy_total >= targetEnergyWh` | STOPPED | Tắt relay, gửi report `completed` |
| CHARGING | Dòng sạc < ngưỡng (pin đầy) | STOPPED | Tắt relay, gửi report `full_charge` |
| CHARGING | Nhận lệnh `STOP_CHARGING` | STOPPED | Tắt relay, gửi report `user_stop` |
| CHARGING | Quá tải phát hiện | FAULT | Tắt relay ngay lập tức, gửi report `overload` |
| CHARGING | Mất kết nối cảm biến | FAULT | Tắt relay, gửi report `station_fault` |
| STOPPED | Report gửi thành công | READY | Xóa session NVS, reset bộ đếm |
| STOPPED | Report thất bại sau 5 retry | READY | Lưu report NVS, retry khi có WiFi |
| FAULT | Nhận lệnh `RESET_STATION` | READY | Reset trạng thái, xóa lỗi |
| READY / STOPPED / FAULT | Nhận `RELAY_ON` | (giữ nguyên state) | Bật relay thủ công |
| READY / STOPPED / FAULT | Nhận `RELAY_OFF` | (giữ nguyên state) | Tắt relay thủ công |
| **CHARGING** | **Nhận `RELAY_OFF`** | **STOPPED** | **Tắt relay, gửi report `user_stop`** |
| **CHARGING** | **Nhận `RELAY_ON`** | (bỏ qua) | Relay đã ON, ghi command `ACCEPTED` |

> **Quy tắc RELAY_OFF khi CHARGING:** Nếu admin gửi lệnh `RELAY_OFF` trong khi trạm đang sạc, ESP32 **phải dừng phiên sạc** (chuyển sang STOPPED và gửi report). Không được chỉ tắt relay mà giữ state CHARGING — sẽ tạo mâu thuẫn với logic server.

> **ESP32 không cần xác thực người gửi command:** Server đã xác thực quyền hạn trước khi ghi command vào RTDB (user chỉ được START/STOP, admin/operator được tất cả). ESP32 chỉ cần kiểm tra `expire_at` và `command.id` trùng lặp, không cần kiểm tra `issued_by`.

### 4.3. Detector sạc đầy (gói "Sạc đầy" — 10k)

Gói sạc `10k` là gói **"Sạc đầy"** — hệ thống sạc cho đến khi pin xe đạt 100%. ESP32 phát hiện pin đầy bằng cách:

1. Sau khi sạc ít nhất **5 phút** (tránh false positive lúc mới khởi động sạc).
2. Lấy giá trị trung bình dòng điện trong **60 giây** gần nhất.
3. Nếu dòng trung bình **< 0.5A** liên tục trong **120 giây** → coi là pin đầy.
4. Tắt relay, gửi report với `stopReason = "full_charge"`.

---

## 5. CÁC TASK FREERTOS

### 5.1. Tổng quan các Task

| Task | Chu kỳ | Ưu tiên | Mô tả |
|------|--------|---------|-------|
| `task_heartbeat` | 10 giây | Thấp | Gửi `heartbeat/last_seen` lên RTDB |
| `task_telemetry` | 2-5 giây | Trung bình | Đọc PZEM-004T, ghi telemetry lên RTDB, kiểm tra an toàn |
| `task_command_listener` | Liên tục (Firebase listener) | Cao | Lắng nghe và xử lý lệnh từ `command` node |
| `task_state_reporter` | Khi state thay đổi | Cao | Cập nhật `status/state` và `status/relay` lên RTDB |
| `task_safety_monitor` | 500ms | Rất cao | Kiểm tra quá dòng, quá áp, nhiệt độ, mất cảm biến |
| `task_session_manager` | 1 giây | Cao | Quản lý phiên sạc, kiểm tra target energy, phát hiện sạc đầy |
| `task_ota_checker` | Khi nhận thông báo qua Firestore listener | Thấp | Kiểm tra và thực hiện OTA update |
| `task_ble_provisioning` | Khi không có WiFi | Trung bình | Phát BLE service để nhận cấu hình WiFi từ app |

### 5.2. Task Heartbeat

```
Mỗi 10 giây:
  1. Lấy Unix timestamp hiện tại
  2. Ghi lên RTDB: stations/{id}/heartbeat/last_seen = timestamp
  3. Log: "[HEARTBEAT] Sent: {timestamp}"
```

**Ý nghĩa:** Server xác định trạm ONLINE/OFFLINE dựa trên heartbeat. Trong web, hàm `mergeStationWithLiveData()` kiểm tra `now - lastSeen > 30` → nếu quá 30 giây → trạm OFFLINE.

### 5.3. Task Telemetry

```
Mỗi 2-5 giây:
  1. Đọc PZEM-004T qua UART:
     - voltage (V), current (A), power (kW), energy (kWh)
  2. Đọc nhiệt độ (nếu có cảm biến)
  3. Ghi lên RTDB: stations/{id}/telemetry = { voltage, current, power, energy_total, temperature }
  4. Nếu đang CHARGING:
     a. Kiểm tra energy_total >= targetEnergyWh → chuyển STOPPED
     b. Kiểm tra full_charge detection (xem section 4.3)
     c. Kiểm tra safety limits (xem section 6)
```

### 5.4. Task Command Listener

ESP32 lắng nghe thay đổi trên node `stations/{id}/command` bằng Firebase RTDB listener:

```
Khi command node thay đổi:
  1. Đọc command data
  2. Kiểm tra expire: if (now > expire_at) → Bỏ qua, ghi status = "EXPIRED"
  3. Kiểm tra trùng lặp: if (command.id == last_processed_id) → Bỏ qua
  4. Xử lý theo type:

     START_CHARGING:
       - Kiểm tra state == READY
       - Lưu sessionId, targetEnergyWh vào bộ nhớ + NVS
       - Reset bộ đếm năng lượng PZEM
       - Bật relay
       - Cập nhật state = CHARGING
       - Ghi command/status = "ACCEPTED"

     STOP_CHARGING:
       - Kiểm tra state == CHARGING
       - Tắt relay
       - Cập nhật state = STOPPED
       - Gửi report HTTP với energyWh thực tế
       - Ghi command/status = "ACCEPTED"

     RELAY_ON:
       - Bật relay GPIO
       - Ghi status/relay = true
       - Ghi command/status = "ACCEPTED"

     RELAY_OFF:
       - Tắt relay GPIO
       - Ghi status/relay = false
       - Ghi command/status = "ACCEPTED"

     RESET_STATION:
       - Tắt relay
       - Xóa session NVS
       - Reset bộ đếm
       - Cập nhật state = READY
       - Ghi command/status = "ACCEPTED"
```

### 5.5. Task BLE Provisioning

Khi ESP32 không kết nối được WiFi (hoặc chưa có cấu hình WiFi), ESP32 khởi động BLE Server:

```
BLE Configuration:
  Service UUID:        4fafc201-1fb5-459e-8fcc-c5c9c331914b
  Characteristic UUID: beb5483e-36e1-4688-b7f5-ea07361b26a8
  Format payload:      "{SSID},{PASSWORD}" (plain text, UTF-8)
```

**Luồng hoạt động:**
1. ESP32 phát BLE advertising.
2. Web Admin (Dashboard) hoặc app sử dụng Web Bluetooth API để quét và kết nối.
3. Admin nhập WiFi SSID + Password trên giao diện, bấm "KẾT NỐI WIFI CHO THIẾT BỊ".
4. Web gửi chuỗi `"{SSID},{PASSWORD}"` qua BLE characteristic.
5. ESP32 nhận, parse, lưu vào NVS, thử kết nối WiFi mới.
6. Nếu thành công → tắt BLE, chuyển sang hoạt động bình thường.

**Các trường hợp kích hoạt BLE:**
- Khởi động lần đầu (chưa có WiFi config).
- WiFi kết nối thất bại sau nhiều lần retry.
- Admin bấm "KẾT NỐI WIFI CHO THIẾT BỊ" trên Dashboard (trạm OFFLINE).

---

## 6. HỆ THỐNG BẢO VỆ AN TOÀN

### 6.1. Bảo vệ quá dòng (Overcurrent Protection)

| Tham số | Giá trị | Nguồn |
|---------|---------|-------|
| `maxCurrent` | Cấu hình từ Admin (mặc định 32A) | RTDB `config/maxCurrent` |
| Ngưỡng cảnh báo | `maxCurrent * 1.1` (110%) | Tính toán local |
| Ngưỡng ngắt | `maxCurrent * 1.2` (120%) | Tính toán local |

**Hành động khi quá dòng:**
1. Ngưỡng cảnh báo (110%): Log cảnh báo, cập nhật telemetry tần suất cao hơn.
2. Ngưỡng ngắt (120%): **Tắt relay ngay lập tức**, chuyển state → `FAULT`, gửi report `overload`.

### 6.2. Bảo vệ quá áp / thấp áp

| Tham số | Ngưỡng |
|---------|--------|
| Quá áp | > 250V |
| Thấp áp | < 180V |

### 6.3. Bảo vệ nhiệt độ

| Tham số | Ngưỡng |
|---------|--------|
| Cảnh báo | > 70°C |
| Ngắt | > 85°C |

### 6.4. Mất kết nối cảm biến

Nếu PZEM-004T không phản hồi sau 3 lần đọc liên tiếp → chuyển state `FAULT`, tắt relay.

### 6.5. Thứ tự ưu tiên an toàn

```
[ƯU TIÊN CAO NHẤT]
1. Quá dòng → Tắt relay NGAY LẬP TỨC (< 100ms)
2. Quá áp / Thấp áp → Tắt relay (< 500ms)
3. Quá nhiệt → Tắt relay (< 1s)
4. Mất cảm biến → Tắt relay (< 3s)
[ƯU TIÊN THẤP NHẤT]
```

> **Nguyên tắc:** Tất cả bảo vệ an toàn xử lý **LOCAL trên ESP32**, KHÔNG phụ thuộc vào kết nối cloud. Relay phải được ngắt ngay khi phát hiện bất thường, dù ESP32 chưa kịp gửi report hoặc mất WiFi.

---

## 7. HỆ THỐNG OTA UPDATE

### 7.1. Kiến trúc OTA

Web Admin cung cấp trang **"OTA UPDATE"** để upload firmware mới (.bin) và phát hành cho các trạm. Dữ liệu OTA được ghi đồng thời vào **Firestore** (cho Web Dashboard theo dõi) và **RTDB** (cho ESP32 nhận lệnh):

- **Firestore** `stations/{id}.ota`: Web Dashboard đọc để hiển thị trạng thái trên giao diện OTA.
- **RTDB** `stations/{id}/ota`: ESP32 lắng nghe node này qua Firebase RTDB listener.

### 7.2. Luồng cập nhật OTA

```
Admin Dashboard                Firebase                    ESP32
      │                           │                          │
      │ 1. Upload firmware.bin    │                          │
      │ ──────────────────────→   │                          │
      │                           │ Storage: ota/firmware_   │
      │                           │ {version}.bin            │
      │                           │                          │
      │ 2a. Cập nhật Firestore    │                          │
      │     stations/{id}/ota     │                          │
      │ ──────────────────────→   │                          │
      │                           │                          │
      │ 2b. Mirror sang RTDB      │                          │
      │     stations/{id}/ota     │                          │
      │ ──────────────────────→   │                          │
      │                           │                          │
      │                           │ 3. RTDB listener         │
      │                           │ ─────────────────────→   │
      │                           │                          │ 4. Check version
      │                           │                          │ 5. Download .bin
      │                           │  ota.status="downloading"│
      │                           │ ←─────────────────────   │
      │                           │                          │ 6. Flash firmware
      │                           │  ota.status="flashing"   │
      │                           │  ota.progress=XX%        │
      │                           │ ←─────────────────────   │
      │                           │                          │ 7. Reboot
      │                           │  ota.status="success"    │
      │                           │ ←─────────────────────   │
```

### 7.3. Node OTA trong RTDB — `stations/{stationId}/ota`

**Hướng:** Cloud → ESP32 (Admin ghi `pending`), ESP32 → Cloud (cập nhật progress/status)

```json
{
  "version": "1713524160000",
  "url": "https://firebasestorage.googleapis.com/v0/b/.../firmware_xxx.bin",
  "status": "pending",
  "progress": 0,
  "error": null,
  "updatedAt": "2026-04-19T00:00:00Z"
}
```

| Trường | Giá trị | Ghi bởi |
|--------|---------|---------|
| `version` | Timestamp version | Admin (Web) |
| `url` | Download URL từ Firebase Storage | Admin (Web) |
| `status` | `pending` → `downloading` → `flashing` → `success` / `error` | `pending` bởi Admin, còn lại bởi ESP32 |
| `progress` | 0 – 100 (%) | ESP32 |
| `error` | Chuỗi lỗi nếu thất bại | ESP32 |

ESP32 cập nhật `status` và `progress` vào **RTDB** (vì ESP32 chỉ kết nối RTDB). Web Dashboard đọc trạng thái OTA từ **Firestore** thông qua Firestore onSnapshot listener trên collection `stations`.

> **Lưu ý đồng bộ:** Hiện tại ESP32 chỉ cập nhật progress vào RTDB. Web Dashboard theo dõi OTA progress qua Firestore listener (trong `OtaUpdate.tsx`). Để Dashboard thấy được progress từ ESP32, cần thêm server-side listener RTDB → Firestore sync, hoặc Dashboard có thể bổ sung đọc RTDB trực tiếp cho mục đích theo dõi OTA progress.

### 7.4. Ràng buộc OTA

- Chỉ chấp nhận file `.bin`, tối đa **2MB**.
- ESP32 kiểm tra version mới khác version hiện tại trước khi tải.
- **KHÔNG** thực hiện OTA khi đang trong trạng thái `CHARGING`.
- Sử dụng ESP-IDF OTA API với dual-partition (app0/app1) để rollback nếu firmware mới lỗi.

---

## 8. QUẢN LÝ PHIÊN SẠC VÀ NVS RECOVERY

### 8.1. Lưu trữ phiên sạc trong NVS

Khi bắt đầu phiên sạc, ESP32 lưu các thông tin quan trọng vào NVS để phục hồi sau khi mất điện hoặc reboot:

```
NVS Namespace: "charging"
  Key "active"       → uint8_t  (1 = đang sạc, 0 = không)
  Key "sessionId"    → string   (ID phiên sạc từ Firestore)
  Key "targetWh"     → uint32_t (mục tiêu năng lượng Wh)
  Key "startEnergy"  → float    (giá trị energy PZEM lúc bắt đầu)
  Key "stationId"    → string   (ID trạm)
  Key "pricePerKwh"  → uint32_t (giá điện VND/kWh)
```

### 8.2. Luồng phục hồi khi reboot

```
Khi ESP32 khởi động:
  1. Kết nối WiFi
  2. Đọc NVS: kiểm tra key "active"
  3. Nếu active == 1:
     a. Đọc sessionId, targetWh, startEnergy từ NVS
     b. Đọc energy hiện tại từ PZEM
     c. Tính energyUsed = currentEnergy - startEnergy
     d. Nếu energyUsed < targetWh:
        → Tiếp tục sạc (giữ relay ON, state = CHARGING)
        → Log: "[RECOVERY] Resumed session {sessionId}"
     e. Nếu energyUsed >= targetWh:
        → Tắt relay, state = STOPPED
        → Gửi report với energyWh = energyUsed
        → Xóa NVS session
  4. Nếu active == 0:
     → state = READY, relay OFF
```

---

## 9. QUẢN LÝ KẾT NỐI WIFI

### 9.1. Chiến lược kết nối

```
Khi khởi động:
  1. Đọc WiFi credentials từ NVS
  2. Nếu có credentials:
     a. Thử kết nối WiFi (timeout 15 giây)
     b. Nếu thành công → hoạt động bình thường
     c. Nếu thất bại → retry 3 lần
     d. Nếu vẫn thất bại → khởi động BLE Provisioning
  3. Nếu không có credentials:
     → Khởi động BLE Provisioning ngay
```

### 9.2. Xử lý mất kết nối khi đang sạc

```
Khi WiFi disconnect trong trạng thái CHARGING:
  1. Tiếp tục sạc (relay giữ ON) — an toàn vẫn được bảo vệ local
  2. Buffer telemetry data locally
  3. Retry WiFi mỗi 5 giây
  4. Nếu reconnect thành công:
     → Gửi lại telemetry buffer
     → Gửi lại heartbeat
     → Cập nhật status
  5. Nếu mất WiFi > 5 phút:
     → Ưu tiên an toàn: tiếp tục sạc nếu dòng/áp ổn định
     → Nếu xảy ra bất thường → tắt relay, lưu session NVS
```

### 9.3. Đồng bộ config từ RTDB

ESP32 lắng nghe node `stations/{id}/config` để nhận cập nhật cấu hình từ Admin:

```
Khi config node thay đổi:
  1. Đọc maxCurrent mới
  2. Cập nhật local safety threshold
  3. Log: "[CONFIG] maxCurrent updated to {value}A"
```

Admin có thể thay đổi `maxCurrent` (1-32A) từ Dashboard, và ESP32 áp dụng ngay lập tức.

---

## 10. GÓI SẠC VÀ TÍNH TOÁN NĂNG LƯỢNG

### 10.1. Các gói sạc

Web hỗ trợ **4 gói sạc**, mỗi gói có mức tiền tạm giữ khác nhau:

| Gói | Số tiền (VND) | Mô tả |
|-----|---------------|-------|
| `10k` | 10,000 | **Sạc đầy** — Sạc đến khi pin đầy, tự ngắt relay. Hoàn tiền dư. |
| `50k` | 50,000 | Hạn mức 50k — Sạc tối đa 50k VND tương đương ~14.29 kWh (giá 3,500đ/kWh) |
| `100k` | 100,000 | Hạn mức 100k — ~28.57 kWh |
| `200k` | 200,000 | Hạn mức 200k — ~57.14 kWh |

### 10.2. Công thức quy đổi

```
targetEnergyWh = (packageAmount / pricePerKwh) * 1000
```

Ví dụ: gói 50k, giá 3,500đ/kWh → targetEnergyWh = (50000 / 3500) * 1000 = **14,286 Wh**

### 10.3. Luồng thanh toán

```
1. User chọn gói sạc trên app
2. Server trừ tiền ví trước (usage_hold)
3. Server gửi command START_CHARGING kèm targetEnergyWh
4. ESP32 sạc cho đến khi đạt targetEnergyWh
5. ESP32 gửi report với energyWh thực tế
6. Server tính:
   - usedAmount = min(prepaidAmount, energyWh / 1000 * pricePerKwh)
   - refundAmount = walletDebitedAmount - usedAmount
7. Server hoàn tiền dư vào ví user (usage_refund)
```

---

## 11. HỆ THỐNG THÔNG BÁO (NOTIFICATION)

Khi ESP32 bắt đầu sạc hoặc dừng sạc, Server phát hiện thay đổi trạng thái qua RTDB listener và gửi notification:

### 11.1. Notification khi bắt đầu sạc

Server phát hiện trạng thái chuyển từ `!= CHARGING` → `CHARGING`:
- Gửi push notification cho user: "Bắt đầu sạc — Trạm {name} đã bắt đầu sạc."
- Gửi notification cho admin: "Theo dõi trạm sạc — Trạm {name} đã bắt đầu sạc."

### 11.2. Notification khi dừng sạc

Server phát hiện trạng thái chuyển từ `CHARGING` → `!= CHARGING`:
- Trạm chuyển sang `STOPPED` / `READY` → Session được đánh dấu `{ status: "Stopped", settlementStatus: "pending_report" }`, đợi report từ ESP32.
- Trạm chuyển sang `FAULT` → Session đánh dấu `pending_report` + stopReason = `station_fault`, gửi cảnh báo cho admin.
- Trạm chuyển sang `OFFLINE` → Session đánh dấu `pending_report` + stopReason = `connection_error`.
- Khi ESP32 gửi report thành công → `settlementStatus: "settled"`, gửi notification chi tiết cho user (sạc đầy, hoàn tất, lỗi...).

### 11.3. Server Reconciliation (Safety Net)

Server chạy hàm `reconcileStuckChargingSessions()` mỗi **60 giây** để xử lý session bị kẹt:

```
Mỗi 60 giây:
  1. Tìm tất cả session Firestore có status = "Charging"
  2. Với mỗi session, kiểm tra RTDB state của trạm tương ứng
  3. Nếu RTDB state != "CHARGING" (đã rời trạng thái sạc):
     → Đánh dấu session = { status: "Stopped", settlementStatus: "pending_report" }
     → Gán stopReason dựa trên RTDB state (FAULT → station_fault, OFFLINE → connection_error, ...)
  4. Đợi ESP32 gửi report để settled, hoặc xử lý thủ công qua Admin
```

Đây là cơ chế backup phía server — đảm bảo không có session nào bị mắc kẹt vĩnh viễn ở trạng thái "Charging" trong Firestore.

### 11.4. Các loại notification

| Loại | Category | Đối tượng | Ví dụ |
|------|----------|-----------|-------|
| Bắt đầu sạc | `user_system` | User | "Trạm A đã bắt đầu sạc" |
| Sạc đầy | `user_completion` | User | "Đã sạc đầy. Số tiền dư 1,400đ được hoàn vào ví" |
| Hoàn thành | `user_completion` | User | "Hoàn thành sạc. Vui lòng rút sạc ra khỏi trạm" |
| Dừng bởi user | `user_system` | User | "Đã dừng sạc theo yêu cầu của bạn" |
| Sự cố | `user_system` | User | "Trạm sạc bị lỗi, vui lòng liên hệ quản trị viên" |
| Theo dõi trạm | `admin_station` | Admin | "Trạm A đã bắt đầu/dừng sạc" |
| Cảnh báo quá tải | `admin_station` | Admin | "Trạm A đã bị quá tải và đã tự động ngắt" |
| Cảnh báo lỗi | `admin_station` | Admin | "Trạm A đã bị lỗi kết nối/lỗi và đã tự ngắt" |

---

## 12. TỔNG KẾT LUỒNG HOẠT ĐỘNG CHÍNH

### 12.1. Luồng sạc hoàn chỉnh

```
┌─────────────────────────────────────────────────────────────────┐
│ 1. User mở app → chọn trạm → chọn gói sạc → bấm XÁC NHẬN    │
│ 2. Server trừ tiền ví, tạo session Firestore                  │
│ 3. Server ghi command START_CHARGING vào RTDB                  │
│ 4. ESP32 nhận command → kiểm tra hợp lệ → bật relay           │
│ 5. ESP32 cập nhật state = CHARGING                             │
│ 6. Server phát hiện CHARGING → gửi notification "Bắt đầu sạc" │
│ 7. ESP32 gửi telemetry liên tục (V, I, P, E)                  │
│ 8. App hiển thị realtime: công suất, năng lượng, thời gian    │
│ 9. ESP32 phát hiện đạt target / pin đầy / user stop           │
│ 10. ESP32 tắt relay, state = STOPPED                           │
│ 11. Server phát hiện STOPPED → đánh dấu session pending_report│
│ 12. ESP32 gửi HTTP report: energyWh, stopReason               │
│ 13. Server tính toán, hoàn tiền dư, cập nhật session          │
│ 14. Server gửi notification "Hoàn thành sạc" / "Sạc đầy"     │
│ 15. ESP32 nhận response OK → state = READY, xóa NVS session   │
└─────────────────────────────────────────────────────────────────┘
```

### 12.2. Luồng xử lý sự cố

```
┌─────────────────────────────────────────────────────────────────┐
│ 1. Safety monitor phát hiện quá dòng/quá áp/mất cảm biến      │
│ 2. Tắt relay NGAY LẬP TỨC (< 100ms cho quá dòng)             │
│ 3. State → FAULT                                               │
│ 4. Gửi report HTTP: stopReason = "overload" / "station_fault" │
│ 5. Server hoàn tiền dư cho user                                │
│ 6. Server gửi cảnh báo cho Admin                               │
│ 7. Admin nhận notification → vào Dashboard → kiểm tra trạm    │
│ 8. Admin bấm RESET_STATION → ESP32 reset về READY             │
└─────────────────────────────────────────────────────────────────┘
```

### 12.3. Luồng BLE WiFi Provisioning

```
┌─────────────────────────────────────────────────────────────────┐
│ 1. ESP32 không kết nối được WiFi → phát BLE advertising        │
│ 2. Admin vào Dashboard → thấy trạm OFFLINE                    │
│ 3. Admin bấm "Chỉnh sửa" → nhập WiFi mới                     │
│ 4. Admin bấm "KẾT NỐI WIFI CHO THIẾT BỊ"                     │
│ 5. Trình duyệt mở Web Bluetooth → chọn ESP32 device           │
│ 6. Web gửi "{SSID},{PASSWORD}" qua BLE characteristic         │
│ 7. ESP32 nhận, lưu NVS, thử kết nối WiFi mới                 │
│ 8. Kết nối thành công → tắt BLE → hoạt động bình thường       │
│ 9. Trạm chuyển từ OFFLINE → READY trên Dashboard              │
└─────────────────────────────────────────────────────────────────┘
```

---

## 13. CẤU HÌNH MẪU (config.h)

```c
#ifndef CONFIG_H
#define CONFIG_H

// ===== DEVICE IDENTITY =====
#define STATION_ID            "ESP32-001"

// ===== WiFi =====
// (Lưu trong NVS, cấu hình qua BLE hoặc hardcode mặc định)
#define DEFAULT_WIFI_SSID     ""
#define DEFAULT_WIFI_PASS     ""

// ===== Firebase RTDB =====
#define FIREBASE_HOST         "tramsacmini-default-rtdb.firebaseio.com"
#define FIREBASE_API_KEY      "AIzaSyXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"

// ===== API Server =====
#define API_BASE_URL          "https://your-server.com"
#define API_REPORT_PATH       "/api/station/report"

// ===== Hardware GPIO =====
#define RELAY_GPIO            26
#define PZEM_RX_GPIO          16
#define PZEM_TX_GPIO          17
#define LED_STATUS_GPIO       2

// ===== BLE Provisioning =====
#define BLE_SERVICE_UUID      "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define BLE_CHAR_UUID         "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// ===== Safety Thresholds =====
#define DEFAULT_MAX_CURRENT   32      // Ampere (có thể cập nhật từ RTDB config)
#define OVERVOLTAGE_LIMIT     250.0f  // Volt
#define UNDERVOLTAGE_LIMIT    180.0f  // Volt
#define OVERTEMP_WARNING      70.0f   // °C
#define OVERTEMP_SHUTDOWN     85.0f   // °C
#define FULL_CHARGE_CURRENT   0.5f    // Ampere (ngưỡng phát hiện pin đầy)
#define FULL_CHARGE_DURATION  120     // Giây (duy trì dòng thấp liên tục)
#define MIN_CHARGE_TIME       300     // Giây (tối thiểu 5 phút trước khi kiểm tra full)

// ===== Timing =====
#define HEARTBEAT_INTERVAL_MS     10000  // 10 giây
#define TELEMETRY_INTERVAL_MS     3000   // 3 giây
#define SAFETY_CHECK_INTERVAL_MS  500    // 500ms
#define WIFI_RECONNECT_INTERVAL   5000   // 5 giây
#define WIFI_MAX_RETRY            3
#define COMMAND_TTL_SECONDS       30
#define PZEM_READ_TIMEOUT_MS      1000

// ===== NVS Keys =====
#define NVS_NAMESPACE         "charging"
#define NVS_KEY_ACTIVE        "active"
#define NVS_KEY_SESSION_ID    "sessionId"
#define NVS_KEY_TARGET_WH     "targetWh"
#define NVS_KEY_START_ENERGY  "startEnergy"
#define NVS_KEY_STATION_ID    "stationId"
#define NVS_KEY_PRICE_KWH     "pricePerKwh"

// ===== OTA =====
#define OTA_MAX_SIZE          (2 * 1024 * 1024)  // 2MB

// ===== LED Status =====
#define LED_BLINK_READY_MS        1000   // Nhấp nháy chậm
#define LED_BLINK_CHARGING_MS     250    // Nhấp nháy nhanh
#define LED_BLINK_FAULT_MS        100    // Nhấp nháy rất nhanh
#define LED_BLINK_BLE_MS          500    // Nhấp nháy trung bình

#endif // CONFIG_H
```

---

## 14. ĐÈN LED TRẠNG THÁI

ESP32 sử dụng LED tại `LED_STATUS_GPIO` (mặc định GPIO 2) để hiển thị trạng thái hoạt động:

| Trạng thái | Pattern LED | Mô tả |
|------------|-------------|-------|
| `READY` | 🟢 Sáng đều | Trạm sẵn sàng, relay OFF |
| `CHARGING` | 🟢 Nhấp nháy nhanh (250ms) | Đang sạc, relay ON |
| `STOPPED` | 🟡 Nhấp nháy 2 lần rồi dừng | Đang gửi report |
| `FAULT` | 🔴 Nhấp nháy rất nhanh (100ms) | Lỗi — cần admin reset |
| BLE Provisioning | 🔵 Nhấp nháy trung bình (500ms) | Đang chờ cấu hình WiFi qua BLE |
| WiFi Connecting | 🟡 Nhấp nháy chậm (1000ms) | Đang kết nối WiFi |
| OTA Updating | 🟡🟢 Xen kẽ nhanh | Đang cập nhật firmware |

---

## 15. BẢNG TƯƠNG THÍCH WEB ↔ ESP32

| Tính năng Web | Đối ứng ESP32 | Giao tiếp |
|---------------|---------------|-----------|
| Dashboard: Hiển thị V, I, P, E | `telemetry` task ghi RTDB | RTDB telemetry node |
| Dashboard: Trạng thái READY/CHARGING/FAULT/OFFLINE | `state_reporter` ghi RTDB | RTDB status node |
| Dashboard: Relay ON/OFF | `command_listener` → GPIO | RTDB command node |
| Dashboard: maxCurrent slider | ESP32 đọc `config/maxCurrent` | RTDB config node |
| Dashboard: BLE WiFi Provision | BLE Server task | Web Bluetooth API ↔ BLE GATT |
| Stations: Hiển thị trạm ONLINE/OFFLINE | `heartbeat` task mỗi 10s | RTDB heartbeat node |
| Stations: Bắt đầu sạc | Nhận `START_CHARGING` command | RTDB command node |
| Charging: Hiển thị realtime telemetry | Telemetry ghi mỗi 2-5s | RTDB telemetry node |
| Charging: Dừng sạc | Nhận `STOP_CHARGING` command | RTDB command node |
| History: Lịch sử sạc | Gửi report → Server lưu Firestore | HTTP POST /api/station/report |
| OTA Update: Cập nhật firmware | OTA checker → download → flash | RTDB ota node + HTTP download |
| Settings: Giá điện | Nhận qua `config` hoặc command | RTDB config / command config |
| Notification: Bắt đầu/dừng sạc | Server detect state change trên RTDB | FCM (server-side) |
| Server Reconciliation | Backup — xử lý session kẹt | Server-side (mỗi 60s) |
