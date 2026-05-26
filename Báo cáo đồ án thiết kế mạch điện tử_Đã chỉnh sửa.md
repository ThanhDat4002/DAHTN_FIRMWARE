# HỌC VIỆN CÔNG NGHỆ BƯU CHÍNH VIỄN THÔNG
----------

# BÁO CÁO BÀI TẬP LỚN
## MÔN ĐỒ ÁN THIẾT KẾ MẠCH ĐIỆN TỬ
### ĐỀ TÀI: THIẾT KẾ VÀ XÂY DỰNG HỆ THỐNG TRẠM SẠC XE ĐIỆN CÓ GIÁM SÁT ĐIỆN NĂNG VÀ THANH TOÁN TRỰC TUYẾN

**Giảng viên hướng dẫn:** ThS. Trương Minh Đức  
**Sinh viên thực hiện:**  
- Nguyễn Văn Thiện – B22DCDT308  
- Dương Thành Đạt – B22DCDT075  

---

## Mục Lục
- [CHƯƠNG 1: TỔNG QUAN ĐỀ TÀI](#chương-1-tổng-quan-đề-tài)
  - [1.1. Đặt vấn đề](#11-đặt-vấn-đề)
  - [1.2. Mục tiêu đề tài](#12-mục-tiêu-đề-tài)
  - [1.3. Đối tượng và phạm vi nghiên cứu](#13-đối-tượng-và-phạm-vi-nghiên-cứu)
  - [1.4. Phương pháp thực hiện](#14-phương-pháp-thực-hiện)
- [CHƯƠNG 2: CƠ SỞ LÝ THUYẾT VÀ CÔNG NGHỆ](#chương-2-cơ-sở-lý-thuyết-và-công-nghệ)
  - [2.1. Vi điều khiển ESP32](#21-vi-điều-khiển-esp32)
  - [2.2. Framework phát triển ESP-IDF](#22-framework-phát-triển-esp-idf)
  - [2.3. Cảm biến điện năng PZEM-004T](#23-cảm-biến-điện-năng-pzem-004t)
  - [2.4. Module Relay điều khiển công suất](#24-module-relay-điều-khiển-công-suất)
  - [2.5. Hệ sinh thái Cloud và IoT (Firebase)](#25-hệ-sinh-thái-cloud-và-iot-firebase)
  - [2.6. Hệ thống Backend và Cổng thanh toán](#26-hệ-thống-backend-và-cổng-thanh-toán)
  - [2.7. Cơ chế đồng bộ thời gian thực qua giao thức NTP](#27-cơ-chế-đồng-bộ-thời-gian-thực-qua-giao-thức-ntp)
- [CHƯƠNG 3: PHÂN TÍCH VÀ THIẾT KẾ HỆ THỐNG](#chương-3-phân-tích-và-thiết-kế-hệ-thống)
  - [3.1. Phân tích yêu cầu hệ thống](#31-phân-tích-yêu-cầu-hệ-thống)
    - [3.1.1. Yêu cầu chức năng](#311-yêu-cầu-chức-năng)
    - [3.1.2. Yêu cầu phi chức năng](#312-yêu-cầu-phi-chức-năng)
  - [3.2. Kiến trúc tổng thể hệ thống](#32-kiến-trúc-tổng-thể-hệ-thống)
  - [3.3. Thiết kế State Machine (FSM)](#33-thiết-kế-state-machine-fsm)
  - [3.4. Thiết kế phần cứng (Hardware Design)](#34-thiết-kế-phần-cứng-hardware-design)
    - [3.4.1. Sơ đồ nguyên lý (Schematic Design)](#341-sơ-đồ-nguyên-lý-schematic-design)
    - [3.4.2. Cấu hình các chân GPIO](#342-cấu-hình-các-chân-gpio)
    - [3.4.3. Thiết kế mạch in (PCB Layout)](#343-thiết-kế-mạch-in-pcb-layout)
    - [3.4.4. Thi công và hoàn thiện](#344-thi-công-và-hoàn-thiện)
  - [3.5. Thiết kế phần mềm và Cơ sở dữ liệu](#35-thiết-kế-phần-mềm-và-cơ-sở-dữ-liệu)
    - [3.5.1. Phân bổ đa nhiệm trên ESP-IDF](#351-phân-bổ-đa-nhiệm-trên-esp-idf)
    - [3.5.2. Cấu trúc dữ liệu Firebase](#352-cấu trúc-dữ-liệu-firebase)
    - [3.5.3. Quy trình thanh toán và quyết toán 2 pha (2-Phase Settlement)](#353-quy-trình-thanh-toán-và-quyết-toán-2-pha-2-phase-settlement)
- [CHƯƠNG 4: XÂY DỰNG HỆ THỐNG](#chương-4-xây-dựng-hệ-thống)
  - [4.1. Thiết kế phần cứng](#41-thiết-kế-phần-cứng)
  - [4.2. Thiết kế Firmware trên ESP32 (ESP-IDF)](#42-thiết-kế-firmware-trên-esp32-esp-idf)
    - [4.2.1. Module Main & Config (Quản trị hệ thống)](#421-module-main--config-quản-trị-hệ-thống)
    - [4.2.2. Module Globals & Types (Cấu trúc dữ liệu)](#422-module-globals--types-cấu-trúc-dữ-liệu)
    - [4.2.3. Module Cloud (Giao tiếp đám mây)](#423-module-cloud-giao-tiếp-đám-mây)
    - [4.2.4. Module Sensor & Safety (Giám sát & Bảo vệ)](#424-module-sensor--safety-giám-sát--bảo-vệ)
    - [4.2.5. Module Relay, UI & NVS (Ngoại vi & Lưu trữ)](#425-module-relay-ui--nvs-ngoại-vi--lưu-trữ)
  - [4.3. Web Dashboard và Backend Server](#43-web-dashboard-và-backend-server)
    - [4.3.1. Giao diện Web App (React & Firebase SDK)](#431-giao-diện-web-app-react--firebase-sdk)
    - [4.3.2. Giao diện và các tính năng chính](#432-giao-diện-và-các-tính-năng-chính)
    - [4.3.3. Backend Server (Express & SePay Integration)](#433-backend-server-express--sepay-integration)
- [CHƯƠNG 5: KIỂM THỬ VÀ ĐÁNH GIÁ](#chương-5-kiểm-thử-và-đánh-giá)
  - [5.1. Kịch bản kiểm thử](#51-kịch-bản-kiểm-thử)
  - [5.2. Kết quả kiểm thử](#52-kết-quả-kiểm-thử)
  - [5.3. Đánh giá hiệu năng và tính ổn định](#53-đánh-giá-hiệu-năng-và-tính-ổn-định)
    - [5.3.1. Phản ứng thời gian thực (Real-time response)](#531-phản-ứng-thời-gian-thực-real-time-response)
    - [5.3.2. Quản lý bộ nhớ và tài nguyên](#532-quản-lý-bộ-nhớ-và-tài-nguyên)
    - [5.3.3. Tính chính xác trong quyết toán tài chính](#533-tính-chính-xác-trong-quyết-toán-tài-chính)
  - [5.4. Đánh giá thực tế từ người dùng](#54-đánh-giá-thực-tế-từ-người-dùng)
- [CHƯƠNG 6: KẾT LUẬN VÀ HƯỚNG PHÁT TRIỂN](#chương-6-kết-luận-và-hướng-phát-triển)
  - [6.1. Kết quả đạt được](#61-kết-quả-đạt-được)
  - [6.2. Hạn chế của đề tài](#62-hạn-chế-của-đề-tài)
  - [6.3. Hướng phát triển](#63-hướng-phát-triển)
- [TÀI LIỆU THAM KHẢO](#tài-liệu-tham-khảo)

---

## CHƯƠNG 1: TỔNG QUAN ĐỀ TÀI

### 1.1. Đặt vấn đề
Sự chuyển dịch từ phương tiện giao thông sử dụng động cơ đốt trong sang xe điện (EV) đang diễn ra mạnh mẽ trên toàn cầu và tại Việt Nam. Tuy nhiên, hạ tầng trạm sạc vẫn là một thách thức lớn. Việc triển khai các trạm sạc công cộng đòi hỏi chi phí đầu tư cao, trong khi các giải pháp sạc tại nhà hoặc văn phòng thường thiếu khả năng giám sát thông minh và cơ chế thanh toán minh bạch.

Hệ thống **EVION** ra đời nhằm giải quyết bài toán này bằng cách cung cấp một giải pháp trạm sạc IoT năng động. Thay vì xây dựng một hệ thống sạc phức tạp theo các tiêu chuẩn công nghiệp đắt đỏ, đề tài tập trung vào việc tạo ra một trạm cấp nguồn xoay chiều thông minh, cho phép sử dụng các bộ sạc đa năng có sẵn để sạc cho các loại xe điện thông dụng, đồng thời tích hợp quản lý năng lượng và thanh toán tự động qua môi trường Cloud.

### 1.2. Mục tiêu đề tài
Mục tiêu chính của đồ án là thiết kế và hiện thực hóa một hệ thống quản lý trạm sạc xe điện thông minh bao gồm:
- **Về phần cứng (Firmware):** Xây dựng bộ điều khiển trung tâm dựa trên chip ESP32 sử dụng framework chuyên nghiệp ESP-IDF. Hệ thống phải có khả năng đọc dữ liệu điện năng thời gian thực từ cảm biến và điều khiển đóng ngắt relay an toàn.
- **Về phần mềm (Cloud & Web App):** Phát triển ứng dụng Web giúp người dùng tìm kiếm trạm sạc, nạp tiền vào ví điện tử cá nhân và theo dõi quá trình sạc trực quan theo thời gian thực.
- **Về quản lý và vận hành:** Tích hợp hệ thống thanh toán tự động qua cổng SePay và cơ chế quyết toán hai pha (2-phase settlement) nhằm đảm bảo tính chính xác tuyệt đối về mặt tài chính.

### 1.3. Đối tượng và phạm vi nghiên cứu
- **Đối tượng nghiên cứu:** Hệ thống nhúng điều khiển thiết bị điện, giao thức truyền thông IoT (HTTPS, REST API) và cơ sở dữ liệu thời gian thực (Firebase Realtime Database & Firestore).
- **Phạm vi:** Trạm sạc cấp nguồn xoay chiều (AC) cho các thiết bị sạc xe điện cá nhân (xe máy điện, ô tô điện). Hệ thống tập trung vào việc giám sát và quản lý từ xa thay vì đi sâu vào các giao thức sạc vật lý phức tạp.

### 1.4. Phương pháp thực hiện
Đề tài được thực hiện dựa trên sự kết hợp của các công nghệ hiện đại:
- **Thiết kế Firmware:** Sử dụng ngôn ngữ C trên nền tảng ESP-IDF, áp dụng cơ chế đa nhiệm (FreeRTOS) ghim tác vụ (task pinning) vào từng nhân CPU để tối ưu hóa việc giám sát an toàn và truyền thông.
- **Hạ tầng dữ liệu:** Sử dụng Firebase Realtime Database để đồng bộ trạng thái thiết bị tức thời và Firestore để quản lý thông tin ví người dùng, lịch sử giao dịch.
- **Phát triển Web & Server:** Sử dụng React cho giao diện người dùng và Express Server (Node.js) làm trung gian xử lý các lệnh điều khiển, webhook thanh toán.

---

## CHƯƠNG 2: CƠ SỞ LÝ THUYẾT VÀ CÔNG NGHỆ

### 2.1. Vi điều khiển ESP32
Trạm sạc sử dụng vi điều khiển trung tâm là ESP32-WROOM-32, một hệ thống trên chip (SoC) tích hợp Wi-Fi và Bluetooth công suất thấp. Đây là lựa chọn tối ưu cho các dự án IoT nhờ khả năng xử lý mạnh mẽ và hệ thống ngoại vi phong phú.

Thông số kỹ thuật chi tiết:
- **Vi xử lý:** Xtensa® Dual-Core 32-bit LX6, xung nhịp lên đến 240 MHz.
- **Bộ nhớ:** 520 KB SRAM nội bộ, tích hợp 8 MB Flash ngoài (thông qua giao tiếp SPI).
- **Kết nối không dây:**
  - Wi-Fi: 802.11 b/g/n (tốc độ lên đến 150 Mbps).
  - Bluetooth: v4.2 BR/EDR và BLE (Bluetooth Low Energy).
- **Ngoại vi:** 34 chân GPIO, 12 kênh ADC 12-bit, 3 cổng UART, 3 cổng SPI, 2 cổng I2C.
- **Nguồn điện:** Hoạt động ổn định ở mức 3.3V, dòng điện tiêu thụ thấp trong các chế độ Sleep.

Với kiến trúc Dual-core, ESP32 cho phép hệ thống vận hành song song: nhân CPU 0 chuyên trách việc xử lý kết nối Wi-Fi và đồng bộ Cloud, nhân CPU 1 tập trung hoàn toàn vào việc giám sát an toàn và đọc cảm biến điện năng.

### 2.2. Framework phát triển ESP-IDF
Dự án được phát triển trên ESP-IDF (Espressif IoT Development Framework) phiên bản v5.x thay vì Arduino IDE. Đây là framework chính thống và chuyên nghiệp nhất của nhà sản xuất Espressif:
- **Hệ điều hành thời gian thực (FreeRTOS):** Cho phép lập trình theo tư duy đa nhiệm (multi-tasking). Các tác vụ (Tasks) được phân quyền ưu tiên (Priority) rõ rệt để điều phối tài nguyên CPU.
- **Task Pinning:** Cơ chế này cho phép ghim một tác vụ cụ thể vào một nhân CPU xác định (Core 0 hoặc Core 1). Điều này cực kỳ quan trọng để đảm bảo tác vụ bảo vệ quá dòng ở Core 1 không bị ảnh hưởng hay trì hoãn bởi các hoạt động mạng ở Core 0.
- **Cấu hình chuyên sâu (SDK Configuration):** Cho phép tinh chỉnh sâu thông số phần cứng như trình giám sát sụt áp nguồn (Brownout detector), kích thước bộ nhớ phân vùng flash (partition table) thông qua file `partitions_8mb.csv`.

### 2.3. Cảm biến điện năng PZEM-004T
Module PZEM-004T được sử dụng như một "đồng hồ đo điện thông minh" hỗ trợ giám sát toàn diện tải AC.
- **Thông số đo lường:**
  - Điện áp: 80 ~ 260 VAC (Sai số 0.5%).
  - Dòng điện: 0 ~ 100 A.
  - Công suất: 0 ~ 23 kW.
  - Năng lượng tích lũy: 0 ~ 9999 kWh.
  - Tần số: 45 ~ 65 Hz.
  - Hệ số công suất (PF): 0.00 ~ 1.00.
- **Giao thức giao tiếp (Modbus RTU):** PZEM-004T sử dụng chuẩn Modbus RTU trên nền UART. Mọi yêu cầu đọc dữ liệu đều đi kèm mã kiểm tra lỗi CRC-16 để đảm bảo dữ liệu không bị sai lệch do nhiễu từ dòng điện lớn trong trạm sạc gây ra.
- **Cách ly quang:** Module tích hợp bộ cách ly quang học để bảo vệ vi điều khiển ESP32 khỏi các xung điện áp cao từ lưới điện 220V.

### 2.4. Module Relay điều khiển công suất
Relay đóng vai trò là "công tắc điện tử" thực thi lệnh đóng/ngắt nguồn cung cấp cho xe điện.
- **Nguyên lý hoạt động:** ESP32 kích mức logic cao qua chân GPIO32 để kích dẫn transistor đệm/opto, cấp nguồn cho cuộn dây của Relay, hút tiếp điểm cơ khí để đóng mạch.
- **Đặc tính kỹ thuật:** Relay chịu tải lên đến 30A/250VAC, phù hợp với các bộ sạc xe điện AC thông dụng hiện nay.
- **An toàn vận hành:** Trạng thái của Relay và tải luôn được firmware giám sát gián tiếp thông qua cảm biến PZEM-004T. Nếu phát hiện lệnh tắt relay đã được phát ra nhưng dòng điện tải vẫn lớn hơn 0.5A (biểu hiện của việc dính tiếp điểm relay cơ khí), hệ thống lập tức báo lỗi kẹt relay cứng (`ERR_RELAY_FAULT`) và đưa thiết bị về trạng thái bảo vệ (`STATE_FAULT`).

### 2.5. Hệ sinh thái Cloud và IoT (Firebase)
Hệ thống sử dụng nền tảng Cloud của Google để quản lý dữ liệu và điều khiển từ xa tức thời.
- **Firebase Realtime Database (RTDB):** Đóng vai trò cốt lõi cho truyền thông thời gian thực. Dữ liệu Telemetry (V, A, W, Wh) và trạng thái của trạm sạc được đẩy lên RTDB theo cấu trúc JSON định kỳ, giúp Web App hiển thị biểu đồ sạc tức thì.
- **Firebase Firestore:** Dùng để lưu trữ các dữ liệu cần bảo mật cao và có cấu trúc: hồ sơ người dùng, số dư ví tiền, danh sách các trạm sạc và lịch sử các phiên sạc chi tiết.

### 2.6. Hệ thống Backend và Cổng thanh toán
Lớp xử lý logic giúp trạm sạc trở thành một sản phẩm thương mại hoàn chỉnh.
- **Express API Server:** Chạy trên Node.js, xử lý các yêu cầu phức tạp như khởi tạo phiên sạc, kiểm tra số dư ví, và xử lý hoàn tiền (refund) sau khi nhận báo cáo sạc từ thiết bị.
- **SePay Integration:** Tích hợp qua cơ chế Webhook. Khi người dùng chuyển khoản quét mã QR thành công, SePay sẽ gửi thông tin giao dịch về Server. Hệ thống tự động bóc tách nội dung chuyển khoản để xác định định danh người dùng và cộng tiền vào ví điện tử ngay lập tức.
- **Xác thực và Bảo mật:** Sử dụng Firebase Auth để định danh người dùng. Phiên sạc sử dụng một `sessionId` duy nhất làm khóa bảo mật bảo vệ báo cáo tài chính từ ESP32 gửi về Server, đóng vai trò như một "bằng chứng xác thực" (proof of knowledge) ngăn chặn việc giả mạo dữ liệu năng lượng.

### 2.7. Cơ chế đồng bộ thời gian thực qua giao thức NTP
Thời gian chính xác là yếu tố bắt buộc để quản lý nhật ký giao dịch và đối soát tài chính. Hệ thống sử dụng giao thức NTP (Network Time Protocol) để đồng bộ giờ từ internet qua thư viện `esp_sntp` của ESP-IDF ngay khi thiết bị kết nối Wi-Fi thành công. Hệ thống tự động đồng bộ lại định kỳ để bù đắp sai số của thạch anh nội bộ, đảm bảo nhãn thời gian (Timestamp) của lịch sử phiên sạc và telemetry trên Firebase là chính xác tuyệt đối.

---

## CHƯƠNG 3: PHÂN TÍCH VÀ THIẾT KẾ HỆ THỐNG

### 3.1. Phân tích yêu cầu hệ thống

#### 3.1.1. Yêu cầu chức năng
- **Tự động cấu hình và kết nối lại:** Hỗ trợ Captive Portal để cấu hình Wi-Fi ban đầu. Khi mất mạng trong lúc đang sạc, thiết bị tự động chuyển sang chế độ `OFFLINE` để tiếp tục phiên sạc an toàn tại chỗ và tự động kết nối lại bằng Exponential Backoff.
- **Giám sát thời gian thực:** Đo lường 6 thông số điện năng (V, I, P, E, Hz, PF) qua cảm biến PZEM-004T và đẩy lên cloud.
- **Điều khiển và An toàn vật lý:** Cho phép đóng/ngắt relay từ xa. Hệ thống tự động ngắt ngay lập tức khi phát hiện quá dòng (>1.5A hoặc giới hạn dòng cấu hình của phiên sạc), quá áp (>380V), thấp áp (<180V), rút súng sạc đột ngột hoặc khi xe đã đầy pin.
- **Thanh toán và Dashboard:** Giao diện Web hiển thị ví tiền, các gói sạc và biểu đồ công suất tiêu thụ theo thời gian thực.

#### 3.1.2. Yêu cầu phi chức năng
- **Tính thời gian thực:** Tác vụ giám sát an toàn (`safety_task`) chạy ở Core 1 với độ ưu tiên cao nhất (Priority 6) để phản ứng ngay với sự cố điện lưới.
- **Độ tin cậy cao:** Sử dụng Task Watchdog (WDT) để giám sát hoạt động của cả hai nhân CPU, tự động khởi động lại nếu phát hiện task bị treo.
- **Bảo mật:** Giao tiếp HTTPS cho các API backend và xác thực người dùng qua Firebase Auth.

### 3.2. Kiến trúc tổng thể hệ thống
Kiến trúc hệ thống bao gồm 3 lớp:
1. **Lớp thiết bị (ESP32):** Thu thập dữ liệu từ cảm biến PZEM-004T, điều khiển cơ cấu chấp hành Relay và cập nhật trạng thái lên Firebase RTDB.
2. **Lớp truyền thông:** Wi-Fi kết nối Internet. Sử dụng HTTPS REST API để gửi báo cáo quyết toán và giao thức Firebase RTDB để đồng bộ điều khiển.
3. **Lớp ứng dụng (Web App & Server):** Giao diện React hiển thị biểu đồ và nhận tương tác từ người dùng. Express Server xử lý Webhook ngân hàng và quyết toán tài chính.

### 3.3. Thiết kế State Machine (FSM)
Vòng đời hoạt động của trạm sạc được quản lý bởi một máy trạng thái nghiêm ngặt:
- `STATE_BOOT`: Khởi động thiết bị, cấu hình phần cứng và nạp dữ liệu cấu hình.
- `STATE_IDLE`: Thiết bị hoạt động ngoại tuyến, chưa kết nối được với Firebase Cloud.
- `STATE_READY`: Đã kết nối Wi-Fi và Firebase, sẵn sàng nhận lệnh bắt đầu sạc.
- `STATE_CHARGING`: Relay đóng, đang cấp điện cho tải, liên tục kiểm tra các điều kiện an toàn và tích phân điện năng.
- `STATE_OFFLINE`: Bị mất kết nối mạng Wi-Fi/Cloud trong khi đang sạc. Phiên sạc vẫn tiếp tục chạy và được bảo vệ bởi logic an toàn tại chỗ.
- `STATE_STOPPED`: Phiên sạc kết thúc hợp lệ (đạt target, pin đầy, rút phích hoặc lệnh dừng), đang gửi báo cáo quyết toán về server.
- `STATE_FAULT`: Phát hiện sự cố nghiêm trọng (quá dòng, quá áp, kẹt relay, lỗi cảm biến), ngắt relay khẩn cấp và khóa hệ thống cho đến khi có lệnh reset từ Admin.

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

### 3.4. Thiết kế phần cứng (Hardware Design)

#### 3.4.1. Sơ đồ nguyên lý (Schematic Design)
Sơ đồ nguyên lý bao gồm khối nguồn hạ áp AC-DC (220VAC sang 5V và 3.3V), khối vi điều khiển ESP32, khối cách ly và đọc cảm biến PZEM-004T qua UART, khối điều khiển Relay qua opto cách ly quang EL817 và transistor đệm. Còi báo Buzzer và các LED trạng thái được kết nối qua các chân GPIO thông qua điện trở hạn dòng.

#### 3.4.2. Cấu hình các chân GPIO
Để quản lý tối ưu các chân GPIO và tránh các chân strapping (như GPIO0, GPIO2, GPIO12, GPIO15 có thể ảnh hưởng quá trình nạp/khởi động), sơ đồ chân được phân bổ như sau:

**Bảng 3.1: Chi tiết phân bổ các chân GPIO trên ESP32**

| STT | Thành phần kết nối | Chân GPIO | Chế độ (I/O) | Chức năng |
| :--- | :--- | :--- | :--- | :--- |
| 1 | Relay Công suất | GPIO32 | Output | Kích đóng/ngắt nguồn điện sạc |
| 2 | Cảm biến PZEM (TX) | GPIO16 | Input (UART2 RX) | Nhận dữ liệu điện năng (V, I, P, E) |
| 3 | Cảm biến PZEM (RX) | GPIO17 | Output (UART2 TX) | Gửi truy vấn Modbus RTU tới cảm biến |
| 4 | Còi chip (Buzzer) | GPIO13 | Output | Phát âm thanh phản hồi, cảnh báo lỗi |
| 5 | Đèn LED Lỗi (Fault) | GPIO21 | Output | LED đỏ nháy cảnh báo khi trạm vào FAULT |
| 6 | Đèn LED Sạc (Charging) | GPIO22 | Output | LED xanh lá nháy chậm khi đang sạc |
| 7 | Đèn LED Sẵn sàng (Status)| GPIO23 | Output | LED trạng thái chung, sáng khi READY |
| 8 | Nút nhấn Reset Wi-Fi | GPIO33 | Input | Nhấn giữ 2s để xóa Wi-Fi và vào Captive Portal |
| 9 | Nút nhấn BOOT | GPIO0 | Input | Nhấn kết hợp GPIO33 trong 3s để Master Reset |

#### 3.4.3. Thiết kế mạch in (PCB Layout)
Mạch in được thiết kế trên phần mềm Altium Designer với các quy tắc an toàn điện:
- **Đường mạch động lực (High-Voltage/High-Current):** Các đường AC nối từ cầu đấu đầu vào tới tiếp điểm Relay và ổ cắm ngõ ra có độ rộng lớn (Net Width > 100 mil), đồng thời được xẻ mạch cách điện (cut-out) giữa đường L và N để tránh phóng điện, phủ thiếc dày để đảm bảo khả năng chịu dòng 30A liên tục.
- **Cách ly nhiễu:** Khối vi xử lý và giao tiếp UART được bố trí ở vùng riêng, cách ly quang hoàn toàn với phần động lực AC thông qua các chip cách ly quang tích hợp trên PZEM và khối kích relay.

#### 3.4.4. Thi công và hoàn thiện
Bo mạch PCB được hàn linh kiện thủ công và rửa sạch bằng dung dịch chuyên dụng. Toàn bộ mạch được đặt cố định trong hộp ABS kỹ thuật chống cháy, có jack nối bấm đầu cos chắc chắn và bố trí các đèn chỉ báo LED cùng nút bấm tiện lợi ở vỏ hộp.

### 3.5. Thiết kế phần mềm và Cơ sở dữ liệu

### 3.5.1. Phân bổ đa nhiệm trên ESP-IDF
Tận dụng kiến trúc 2 nhân của ESP32, các tác vụ FreeRTOS được phân bổ cụ thể:
- **Core 1 (Hardware Core):** Chạy các tác vụ trực tiếp với phần cứng:
  - `safety_task` (Độ ưu tiên 6 - Cao nhất): Giám sát các lỗi an toàn điện lưới.
  - `sensor_task` (Độ ưu tiên 5): Đọc dữ liệu từ PZEM-004T chu kỳ 200ms.
  - `ui_task` (Độ ưu tiên 2): Điều khiển LED trạng thái và Buzzer.
- **Core 0 (Network Core):** Chạy các tác vụ truyền thông:
  - `cloud_publish_task` (Độ ưu tiên 4): Đẩy telemetry lên Firebase RTDB mỗi 5 giây.
  - `cloud_poll_task` (Độ ưu tiên 4): Polling đọc lệnh sạc từ node `/command` mỗi 1 giây.
  - `network_task` (Độ ưu tiên 3): Quản lý Wi-Fi, tự động kết nối lại và xử lý portal fallback.
  - `wifi_reset_button_task` (Độ ưu tiên 1): Quét nút nhấn GPIO33 và GPIO0 để reset.

#### 3.5.2. Cấu trúc dữ liệu Firebase
Hệ thống sử dụng mô hình kết hợp nhằm tối ưu tốc độ đồng bộ thời gian thực:
- **Realtime Database (RTDB):** Tổ chức dạng JSON dưới đường dẫn `stations/{deviceId}/`:
  - `telemetry`: `{ "voltage", "current", "power", "energy_total_wh" }` (Đơn vị Wh giúp tránh sai số số thực dấu phẩy động).
  - `status`: `{ "relay", "state", "state_detail", "error_code" }` (Trạng thái và lỗi hiện tại).
  - `current_session`: `{ "session_id", "user_id", "start_time", "end_time", "energy_used_wh", "end_reason" }` (Thông tin phiên sạc hiện tại).
  - `heartbeat`: `{ "last_seen", "uptime_seconds" }` (Báo trạng thái hoạt động của thiết bị).
  - `command`: Lắng nghe lệnh từ Web App: `START_CHARGING`, `STOP_CHARGING`, `RESET_STATION`, `UPDATE_FIRMWARE`, `UPDATE_CONFIG`.
  - `config`: Lưu cấu hình dòng cực đại `maxCurrent`.
- **Firestore Database:** Lưu trữ dữ liệu lâu dài:
  - Collection `wallets`: Số dư tài khoản người dùng (`walletBalance`).
  - Collection `sessions`: Lưu trữ lịch sử tất cả phiên sạc phục vụ tra soát.

#### 3.5.3. Quy trình thanh toán và quyết toán 2 pha (2-Phase Settlement)
Quy trình được thiết kế nhằm hạn chế tối đa việc thất thoát tài chính:
1. **Pha 1 (Tạm giữ tiền):** Người dùng chọn gói sạc (ví dụ gói 50k). Server kiểm tra số dư ví. Nếu đủ điều kiện, server sẽ trừ tạm giữ số tiền 50k trong ví, tạo phiên sạc trên Firestore và gửi lệnh `START_CHARGING` kèm `targetEnergyWh` (~14,286 Wh) vào RTDB của trạm sạc.
2. **Pha 2 (Quyết toán thực tế):** Khi kết thúc sạc, ESP32 ngắt relay và gửi bản tin POST chứa điện năng thực tiêu thụ (Wh) về server qua endpoint `/api/station/report`. Server dựa trên số Wh thực tế để tính tiền sạc chính xác, thực hiện cập nhật lịch sử, và hoàn lại phần tiền dư (refund) ngay lập tức vào ví người dùng.

---

## CHƯƠNG 4: XÂY DỰNG HỆ THỐNG

### 4.1. Thiết kế phần cứng
Thiết kế phần cứng hoàn thiện đáp ứng việc sạc AC an toàn. Bo mạch PCB được đặt gọn trong hộp ABS. Các terminal đầu vào và ngõ ra được bắt vít chắc chắn, kết nối qua cáp tải lớn. Các đèn LED báo hiệu trạng thái hoạt động và còi chíp buzzer giúp người dùng dễ dàng nắm bắt trạng thái hoạt động trực quan.

### 4.2. Thiết kế Firmware trên ESP32 (ESP-IDF)
Mã nguồn được tổ chức cấu trúc module hóa cao giúp dễ quản lý và bảo trì:

#### 4.2.1. Module Main & Config (Quản trị hệ thống)
- **`main.c`:** Chứa hàm khởi động `app_main()`. Thiết lập tuần tự hệ thống lưu trữ NVS, các biến toàn cục, sau đó khởi tạo các Task FreeRTOS trên từng nhân CPU tương ứng.
- **`config.h`:** Chứa toàn bộ cấu hình hằng số hệ thống bao gồm sơ đồ chân GPIO, chu kỳ định thời của các task, ngưỡng bảo vệ dòng áp mặc định, và NVS keys/namespaces.
- **Đặc biệt (Xử lý khi mất điện đột ngột):** Khi khởi động, firmware kiểm tra khóa `active` của session trong NVS. Nếu phát hiện trước khi reboot trạm đang sạc dở (active = 1), để bảo vệ an toàn tối đa cho xe điện, hệ thống **không tự ý sạc tiếp** mà chủ động ngắt relay, xóa session lưu trữ trong NVS, khôi phục trạng thái an toàn chờ lệnh mới.

#### 4.2.2. Module Globals & Types (Cấu trúc dữ liệu)
- **`types.h`:** Định nghĩa các kiểu dữ liệu dùng chung như enum trạng thái hệ thống (`system_state_t`), mã lỗi (`error_code_t`), stop reason (`stop_reason_t`).
- **`globals.c/.h`:** Quản lý các biến toàn cục và cung cấp các hàm getter/setter thread-safe thông qua hệ thống Mutex nhằm tránh xung đột dữ liệu giữa các task.

#### 4.2.3. Module Cloud (Giao tiếp đám mây)
- **`cloud.c`:** Quản lý vòng đời kết nối cloud. Task `cloud_publish_task` đẩy dữ liệu status/telemetry định kỳ 5s. Task `cloud_poll_task` đọc node `/command` mỗi 1s.
- **`cloud_http.c`:** Thực hiện đóng gói các HTTP request (GET/PATCH/POST/PUT) tới Firebase REST API. Sử dụng mutex `s_firebase_mutex` để serialize các yêu cầu mạng, tránh tranh chấp tài nguyên bộ thư viện của ESP-IDF.
- **`cloud_report.c`:** Chịu trách nhiệm gửi kết quả phiên sạc về Backend Server của Web qua giao thức HTTPS POST `/api/station/report`.

#### 4.2.4. Module Sensor & Safety (Giám sát & Bảo vệ)
- **`sensor.c`:** Giao tiếp với cảm biến PZEM-004T qua chuẩn Modbus RTU trên cổng UART2. Module thực hiện lọc nhiễu khung truyền Modbus thông qua thuật toán CRC-16. Nếu việc đọc cảm biến thất bại liên tiếp 3 lần, hệ thống sẽ cố gắng reset driver UART để tự phục hồi kết nối cảm biến.
- **`safety.c`:** Thực thi logic bảo vệ thời gian thực trên Core 1. Module liên tục quét các lỗi bao gồm:
  - Phát hiện kẹt tiếp điểm relay cơ khí (`safety_check_relay_stuck_on`).
  - Quá dòng tải sạc: Nếu dòng điện đo được vượt ngưỡng dòng cho phép, relay lập tức bị ngắt trong vòng 200ms (sau 1 mẫu đo).
  - Quá áp / thấp áp lưới điện: Tự động ngắt relay nếu điện áp lưới vượt quá 380V hoặc sụt xuống dưới 180V liên tục trong 3 mẫu đo (600ms).
  - Pin sạc đầy: Tự ngắt nếu dòng sạc giảm xuống mức dòng rò trickle (<1.5mA) liên tục trong 5 phút.
  - Chống rút trộm súng sạc: Khi đang sạc công suất ổn định, nếu phát hiện dòng sạc đột ngột rơi về 0A liên tục trong 2 giây, hệ thống kết luận súng sạc bị rút đột ngột và đóng phiên sạc.

#### 4.2.5. Module Relay, UI & NVS (Ngoại vi & Lưu trữ)
- **`relay.c`:** Đóng ngắt GPIO32 điều khiển nguồn điện.
- **`ui.c`:** Đèn trạng thái (READY sáng LED status GPIO23, CHARGING chớp LED sạc GPIO22, FAULT chớp LED lỗi GPIO21) và Buzzer.
- **`wifi.c` (wifi_reset_button_task):** Task quét nút nhấn GPIO33 (Reset Wi-Fi) và GPIO0 (BOOT).
  - Nhấn giữ nút Reset Wi-Fi (GPIO33) > 2s khi thiết bị đang ở trạng thái chờ: Xóa thông tin Wi-Fi cũ trong NVS và khởi động lại thiết bị vào chế độ Captive Portal.
  - Nhấn tổ hợp cả hai nút (GPIO33 + GPIO0) > 3s: Thực hiện Master Reset, xóa phiên sạc NVS, xóa cờ lỗi FAULT giúp khôi phục hệ thống tại chỗ.
- **`wifi_portal.c`:** Khi khởi động không tìm thấy Wi-Fi cũ hoặc do lệnh reset, thiết bị tạo một mạng AP mở tên `EVION_Setup`. Người dùng kết nối vào Wi-Fi này sẽ tự động được chuyển hướng (DNS redirect) đến trang cấu hình tại địa chỉ IP `192.168.4.1` để nhập thông tin mạng Wi-Fi mới và ID của trạm sạc.

---

## CHƯƠNG 5: KIỂM THỬ VÀ ĐÁNH GIÁ

### 5.1. Kịch bản kiểm thử
Các kịch bản kiểm thử được thiết lập tập trung vào tính chính xác của dữ liệu đo lường và khả năng phản ứng bảo vệ an toàn:
- **Kiểm thử đo lường:** Đối chiếu thông số dòng điện, điện áp đo từ cảm biến PZEM-004T với đồng hồ vạn năng và ampe kìm đo trực tiếp dòng tải.
- **Kiểm thử thời gian phản ứng lệnh:** Đo thời gian trễ từ khi người dùng xác nhận sạc trên Web App cho đến khi relay thực tế đóng mạch.
- **Kiểm thử ngắt sự cố (Fault Injection):** Giả lập quá dòng bằng cách thay đổi giá trị dòng tối đa hoặc cấp tải giả lập vượt ngưỡng dòng cho phép, giả lập sụt áp lưới và đo thời gian ngắt relay thực tế.

### 5.2. Kết quả kiểm thử

**Bảng 5.1: Kết quả thử nghiệm các kịch bản thực tế**

| Kịch bản thử nghiệm | Mô tả thử nghiệm | Kết quả | Chi tiết & Ghi chú |
| :--- | :--- | :--- | :--- |
| Độ chính xác PZEM | Đo điện lưới AC và tải sạc thực tế | **Đạt** | Sai số điện áp <0.5%, dòng điện <1.0% so với đồng hồ đối chứng. |
| Độ trễ lệnh điều khiển | Nhấn sạc trên Web -> Relay trạm đóng | **3 giây** | Phụ thuộc tốc độ internet và chu kỳ polling của thiết bị (1s). |
| Bảo vệ quá dòng | Cấp dòng tải vượt quá giới hạn cấu hình | **Đạt** | Relay ngắt cực nhanh sau 1 mẫu đo (200ms), báo lỗi OVER_CURRENT. |
| Tự ngắt khi sạc đầy | Gói sạc đầy, dòng tải giảm về <1.5mA | **Đạt** | Tự động ngắt relay sau 5 phút đo dòng trickle liên tục, báo cáo kết thúc. |
| Chống rút trộm súng sạc | Rút phích cắm khi xe đang sạc dở | **Đạt** | Trạm phát hiện dòng sạc rơi về 0A đột ngột, ngắt relay sau 2s, kết thúc phiên. |
| Bảo vệ sụt áp lưới điện| Sụt điện áp AC giả lập dưới 180V | **Đạt** | Relay ngắt bảo vệ sau 600ms (3 mẫu đo), báo lỗi UNDER_VOLTAGE. |
| Mất Wi-Fi khi đang sạc | Ngắt kết nối Wi-Fi khi xe đang sạc | **Đạt** | Trạm chuyển trạng thái sạc OFFLINE, tự động lưu thông số sạc vào NVS định kỳ. |

### 5.3. Đánh giá hiệu năng và tính ổn định

#### 5.3.1. Phản ứng thời gian thực (Real-time response)
Nhờ kiến trúc Dual-core phân chia tác vụ chuyên biệt, hoạt động bảo vệ của trạm sạc có tính độc lập cao. Các tác vụ cloud chạy ở Core 0 hoàn toàn không ảnh hưởng tới tiến trình đo đạc và bảo vệ quá dòng chạy ở Core 1. Trạm sạc phản ứng tức thời (<200ms cho quá dòng và <600ms cho sụt áp) đảm bảo an toàn tuyệt đối cho thiết bị xe sạc.

#### 5.3.2. Quản lý bộ nhớ và tài nguyên
Hệ thống sử dụng tài nguyên ổn định. Nhờ cơ chế giải phóng vùng nhớ cJSON hợp lý, ESP32 duy trì dung lượng Heap trống ổn định ở mức ~120KB trong suốt quá trình vận hành liên tục. Cơ chế Task Watchdog được cấu hình thời gian 3 giây hoạt động hiệu quả, giúp thiết bị tự phục hồi nếu phát hiện tình trạng treo tác vụ.

#### 5.3.3. Tính chính xác trong quyết toán tài chính
Quy trình thanh toán 2 pha qua SePay hoạt động chính xác. Nhờ việc lưu trữ điện năng dạng số nguyên Watt-giờ (Wh) và cơ chế đối soát session ID duy nhất, hệ thống quyết toán hoàn toàn chính xác số tiền sạc thực tế tiêu thụ, hoàn trả chính xác tiền dư vào ví người dùng mà không xảy ra tình trạng sai số hay lỗi giao dịch.

---

## CHƯƠNG 6: KẾT LUẬN VÀ HƯỚNG PHÁT TRIỂN

### 6.1. Kết quả đạt được
Đồ án đã thiết kế và xây dựng thành công trạm sạc xe điện thông minh đáp ứng đầy đủ các tiêu chuẩn kỹ thuật:
- **Thiết kế phần cứng hoàn thiện:** Thi công thành công bo mạch điều khiển trung tâm ESP32 tích hợp cảm biến điện năng PZEM-004T có độ chính xác cao và relay chịu tải 30A an toàn, đặt trong hộp nhựa ABS chống cháy.
- **Firmware tối ưu:** Xây dựng phần mềm nhúng dựa trên ESP-IDF với kiến trúc FreeRTOS đa nhiệm, xử lý tốt các tình huống an toàn lưới điện tại chỗ, hỗ trợ khôi phục an toàn sau mất điện, cấu hình Wi-Fi qua Captive Portal thân thiện.
- **Hệ thống phần mềm Cloud đồng bộ:** Triển khai quy trình thanh toán ví điện tử tự động và cơ chế quyết toán 2 pha qua SePay, giúp người dùng theo dõi và quản lý quá trình sạc thời gian thực từ xa.

### 6.2. Hạn chế của đề tài
- **Độ trễ truyền thông:** Việc trạm sạc sử dụng cơ chế HTTP Polling để đọc lệnh từ Firebase RTDB vẫn tạo ra độ trễ điều khiển nhỏ (khoảng 1-2 giây) tùy thuộc vào chất lượng đường truyền mạng Wi-Fi tại trạm.
- **Tính tương tác tại chỗ:** Trạm sạc hiện tại hoàn toàn phụ thuộc vào việc điều khiển qua Web App trên điện thoại thông minh, chưa hỗ trợ phương thức xác thực nhanh trực tiếp tại trạm sạc như thẻ từ (RFID).

### 6.3. Hướng phát triển
- **Nâng cấp giao thức truyền thông:** Chuyển đổi từ HTTP sang giao thức MQTT để giảm thiểu độ trễ nhận lệnh xuống mức mili giây, tiết kiệm băng thông và tối ưu tài nguyên của vi điều khiển khi triển khai số lượng trạm sạc lớn.
- **Tích hợp module đọc thẻ RFID:** Bổ sung đầu đọc thẻ RFID để người dùng có thể quẹt thẻ bắt đầu/dừng sạc nhanh chóng trực tiếp tại trạm sạc mà không cần dùng điện thoại, phù hợp với các bãi đỗ xe công cộng, chung cư.

---

## TÀI LIỆU THAM KHẢO
1. *ESP-IDF Programming Guide v5.x*, Espressif Systems.
2. *Firebase Realtime Database & Cloud Firestore Documentation*, Google Cloud.
3. *PZEM-004T v3.0 User Manual & Modbus RTU Protocol Specification*, Peacefair.
4. *Tài liệu tích hợp API Cổng thanh toán SePay*, SePay.
