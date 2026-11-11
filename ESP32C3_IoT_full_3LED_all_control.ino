
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <math.h>

// ==============================================================================
// 1. CẤU HÌNH WIFI
// ==============================================================================

// Điền thông tin Wi-Fi của bạn.
const char* WIFI_SSID = "P206";
const char* WIFI_PASSWORD = "P206@123";

// ==============================================================================
// 2. CẤU HÌNH MQTT BROKER
// ==============================================================================

// IP máy tính đang chạy Mosquitto.
// Máy tính và ESP32 phải kết nối cùng mạng LAN.
const char* MQTT_HOST = "192.168.0.100";

const uint16_t MQTT_PORT = 1886;

// Điền tài khoản MQTT của bạn.
const char* mqtt_user = "nam";
const char* mqtt_password = "123";

// Các topic MQTT.
const char* TOPIC_SENSOR = "Data_Sensors";
const char* TOPIC_CONTROL = "Device_Control";
const char* TOPIC_RESPONSE = "Device_Response";

// ==============================================================================
// 3. CẤU HÌNH CHÂN ESP32-C3 MINI
// ==============================================================================

// Cảm biến AHT20.
#define AHT20_SDA 8
#define AHT20_SCL 9

// Cảm biến ánh sáng TEMT6000.
#define TEMT6000_PIN 0

// Ba đèn LED.
#define LED_ANH_SANG 7
#define LED_DO_AM 4
#define LED_NHIET_DO 3

/*
    Quy ước deviceId:

    0 = Điều khiển cả 3 đèn.
    1 = Đèn ánh sáng (GPIO7).
    2 = Đèn độ ẩm (GPIO4).
    3 = Đèn nhiệt độ (GPIO3).
*/

// ==============================================================================
// 4. KHAI BÁO ĐỐI TƯỢNG
// ==============================================================================

Adafruit_AHTX0 aht;

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// Thời gian gửi dữ liệu cảm biến.
unsigned long lastSensorSend = 0;

// Chu kỳ gửi cảm biến: 30 giây.
const unsigned long SENSOR_INTERVAL_MS = 30000UL;

// ==============================================================================
// 5. KẾT NỐI WIFI
// ==============================================================================

void connectWiFi() {

    if (WiFi.status() == WL_CONNECTED) {
        return;
    }

    Serial.println();
    Serial.print("Dang ket noi Wi-Fi: ");
    Serial.println(WIFI_SSID);

    WiFi.mode(WIFI_STA);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED) {

        delay(500);

        Serial.print(".");

    }

    Serial.println();

    Serial.println("Wi-Fi da ket noi.");

    Serial.print("IP ESP32-C3: ");

    Serial.println(WiFi.localIP());
}

// ==============================================================================
// 6. GỬI PHẢN HỒI TRẠNG THÁI THIẾT BỊ
// ==============================================================================

void publishResponse(
    int deviceId,
    const char* state,
    const char* status
) {

    if (!mqtt.connected()) {
        Serial.println("MQTT chua ket noi, khong the gui phan hoi.");
        return;
    }

    char payload[128];

    snprintf(
        payload,
        sizeof(payload),
        "{\"deviceId\":%d,\"state\":\"%s\",\"status\":\"%s\"}",
        deviceId,
        state,
        status
    );

    bool sent = mqtt.publish(
        TOPIC_RESPONSE,
        payload
    );

    Serial.print("Device_Response: ");

    Serial.println(payload);

    if (!sent) {
        Serial.println("Canh bao: gui phan hoi MQTT that bai.");
    }
}

// ==============================================================================
// 7. XỬ LÝ LỆNH ĐIỀU KHIỂN TỪ WEBSITE
// ==============================================================================

// Ví dụ điều khiển cả 3 đèn:
// {"deviceId":0,"action":"ON"}

// Ví dụ điều khiển một đèn:
// {"deviceId":1,"action":"OFF"}

void mqttCallback(
    char* topic,
    byte* payload,
    unsigned int length
) {

    String message;

    message.reserve(length + 1);

    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }

    Serial.println();
    Serial.println("================================");

    Serial.print("Nhan topic: ");
    Serial.println(topic);

    Serial.print("Noi dung: ");
    Serial.println(message);

    // Chỉ xử lý topic điều khiển.
    if (String(topic) != TOPIC_CONTROL) {
        return;
    }

    // ----------------------------------------------------------
    // PHÂN TÍCH DEVICE ID
    // ----------------------------------------------------------

    int idPos = message.indexOf("\"deviceId\"");

    int colonPos = (idPos >= 0)
        ? message.indexOf(':', idPos)
        : -1;

    int deviceId = (colonPos >= 0)
        ? message.substring(colonPos + 1).toInt()
        : -1;

    // ----------------------------------------------------------
    // PHÂN TÍCH ACTION
    // ----------------------------------------------------------

    String action;

    int actionPos = message.indexOf("\"action\"");

    int actionColon = (actionPos >= 0)
        ? message.indexOf(':', actionPos)
        : -1;

    int quote1 = (actionColon >= 0)
        ? message.indexOf('"', actionColon + 1)
        : -1;

    int quote2 = (quote1 >= 0)
        ? message.indexOf('"', quote1 + 1)
        : -1;

    if (quote1 >= 0 && quote2 > quote1) {

        action = message.substring(
            quote1 + 1,
            quote2
        );

        action.trim();

        action.toUpperCase();
    }

    // ----------------------------------------------------------
    // KIỂM TRA LỆNH
    // ----------------------------------------------------------

    if (action != "ON" && action != "OFF") {

        publishResponse(
            deviceId,
            "UNKNOWN",
            "FAILED"
        );

        Serial.println("Lenh khong hop le.");

        return;
    }

    const int level = (action == "ON")
        ? HIGH
        : LOW;

    const char* stateText = (action == "ON")
        ? "ON"
        : "OFF";

    // ----------------------------------------------------------
    // DEVICE ID = 0: ĐIỀU KHIỂN CẢ 3 ĐÈN
    // ----------------------------------------------------------

    if (deviceId == 0) {

        digitalWrite(LED_ANH_SANG, level);

        digitalWrite(LED_DO_AM, level);

        digitalWrite(LED_NHIET_DO, level);

        bool allMatch =
            (digitalRead(LED_ANH_SANG) == level) &&
            (digitalRead(LED_DO_AM) == level) &&
            (digitalRead(LED_NHIET_DO) == level);

        publishResponse(
            0,
            allMatch ? stateText : "UNKNOWN",
            allMatch ? "SUCCESS" : "FAILED"
        );

        Serial.println("Da dieu khien ca 3 den.");

        return;
    }

    // ----------------------------------------------------------
    // DEVICE ID = 1, 2, 3: ĐIỀU KHIỂN TỪNG ĐÈN
    // ----------------------------------------------------------

    int ledPin = -1;

    if (deviceId == 1) {

        ledPin = LED_ANH_SANG;

    } else if (deviceId == 2) {

        ledPin = LED_DO_AM;

    } else if (deviceId == 3) {

        ledPin = LED_NHIET_DO;

    }

    if (ledPin < 0) {

        publishResponse(
            deviceId,
            "UNKNOWN",
            "FAILED"
        );

        Serial.println("deviceId khong hop le.");

        return;
    }

    digitalWrite(ledPin, level);

    bool isOn = (digitalRead(ledPin) == HIGH);

    publishResponse(
        deviceId,
        isOn ? "ON" : "OFF",
        "SUCCESS"
    );

    Serial.println("Da dieu khien LED thanh cong.");
}

// ==============================================================================
// 8. KẾT NỐI MQTT
// ==============================================================================

void connectMQTT() {

    while (!mqtt.connected()) {

        // Đảm bảo Wi-Fi được kết nối trước.
        connectWiFi();

        String clientId =
            "ESP32C3-IoT-" +
            String((uint32_t)ESP.getEfuseMac(), HEX);

        Serial.println();

        Serial.print("Dang ket noi MQTT... ");

        if (
            mqtt.connect(
                clientId.c_str(),
                mqtt_user,
                mqtt_password
            )
        ) {

            Serial.println("Da ket noi MQTT.");

            // --------------------------------------------------
            // ĐĂNG KÝ TOPIC ĐIỀU KHIỂN
            // --------------------------------------------------

            bool subscribed = mqtt.subscribe(
                TOPIC_CONTROL
            );

            Serial.print("Subscribe topic: ");

            Serial.println(TOPIC_CONTROL);

            if (!subscribed) {

                Serial.println(
                    "Canh bao: Subscribe Device_Control that bai."
                );

            }

            // --------------------------------------------------
            // THÔNG BÁO ESP32 ONLINE
            // --------------------------------------------------

            mqtt.publish(
                TOPIC_RESPONSE,
                "{\"deviceId\":0,\"state\":\"ONLINE\",\"status\":\"SUCCESS\"}"
            );

            // --------------------------------------------------
            // GỬI DỮ LIỆU CẢM BIẾN NGAY SAU KHI KẾT NỐI LẠI
            // --------------------------------------------------

            lastSensorSend =
                millis() - SENSOR_INTERVAL_MS;

            Serial.println(
                "ESP32 san sang gui du lieu cam bien."
            );

        } else {

            Serial.print(
                "Ket noi MQTT that bai, state="
            );

            Serial.print(mqtt.state());

            Serial.println(
                ". Thu lai sau 3 giay."
            );

            delay(3000);
        }
    }
}

// ==============================================================================
// 9. ĐỌC VÀ GỬI DỮ LIỆU CẢM BIẾN
// ==============================================================================

void readAndPublishSensors() {

    if (!mqtt.connected()) {
        Serial.println("MQTT chua ket noi, bo qua lan gui.");
        return;
    }

    // ----------------------------------------------------------
    // ĐỌC DỮ LIỆU AHT20
    // ----------------------------------------------------------

    sensors_event_t humidityEvent;
    sensors_event_t temperatureEvent;

    aht.getEvent(
        &humidityEvent,
        &temperatureEvent
    );

    float humidity =
        humidityEvent.relative_humidity;

    float temperature =
        temperatureEvent.temperature;

    // ----------------------------------------------------------
    // ĐỌC DỮ LIỆU TEMT6000
    // ----------------------------------------------------------

    int lightRaw = analogRead(TEMT6000_PIN);

    // ----------------------------------------------------------
    // KIỂM TRA DỮ LIỆU
    // ----------------------------------------------------------

    if (
        isnan(temperature) ||
        isnan(humidity)
    ) {

        Serial.println(
            "Loi doc AHT20; bo qua lan gui nay."
        );

        return;
    }

    // ----------------------------------------------------------
    // ĐÓNG GÓI JSON
    // ----------------------------------------------------------

    char payload[180];

    snprintf(
        payload,
        sizeof(payload),
        "{\"temperature\":%.2f,\"humidity\":%.2f,\"light\":%d}",
        temperature,
        humidity,
        lightRaw
    );

    // ----------------------------------------------------------
    // GỬI DỮ LIỆU ĐẾN MQTT BROKER
    // ----------------------------------------------------------

    bool sent = mqtt.publish(
        TOPIC_SENSOR,
        payload
    );

    Serial.println();
    Serial.println("================================");

    Serial.println("DU LIEU CAM BIEN:");

    Serial.print("Nhiet do: ");
    Serial.print(temperature);
    Serial.println(" °C");

    Serial.print("Do am: ");
    Serial.print(humidity);
    Serial.println(" %");

    Serial.print("Anh sang ADC: ");
    Serial.println(lightRaw);

    Serial.print("JSON: ");
    Serial.println(payload);

    if (sent) {

        Serial.println(
            "Publish MQTT thanh cong."
        );

    } else {

        Serial.println(
            "Publish MQTT that bai."
        );
    }
}

// ==============================================================================
// 10. KHỞI ĐỘNG ESP32
// ==============================================================================

void setup() {

    Serial.begin(115200);

    delay(500);

    Serial.println();

    Serial.println("================================");

    Serial.println("   ESP32-C3 IOT MONITORING");

    Serial.println("================================");

    // ----------------------------------------------------------
    // CẤU HÌNH 3 LED
    // ----------------------------------------------------------

    pinMode(LED_ANH_SANG, OUTPUT);

    pinMode(LED_DO_AM, OUTPUT);

    pinMode(LED_NHIET_DO, OUTPUT);

    // Khởi động tất cả đèn ở trạng thái OFF.
    digitalWrite(LED_ANH_SANG, LOW);

    digitalWrite(LED_DO_AM, LOW);

    digitalWrite(LED_NHIET_DO, LOW);

    // ----------------------------------------------------------
    // CẤU HÌNH ADC
    // ----------------------------------------------------------

    analogReadResolution(12);

    // ADC 12 bit: 0 - 4095.
    pinMode(TEMT6000_PIN, INPUT);

    // ----------------------------------------------------------
    // KHỞI ĐỘNG AHT20
    // ----------------------------------------------------------

    Wire.begin(
        AHT20_SDA,
        AHT20_SCL
    );

    if (!aht.begin()) {

        Serial.println(
            "Khong tim thay AHT20."
        );

        Serial.println(
            "Kiem tra SDA=GPIO8, SCL=GPIO9 va day nguon."
        );

        while (true) {
            delay(1000);
        }
    }

    Serial.println("AHT20 da san sang.");

    // ----------------------------------------------------------
    // CẤU HÌNH MQTT
    // ----------------------------------------------------------

    mqtt.setServer(
        MQTT_HOST,
        MQTT_PORT
    );

    mqtt.setCallback(
        mqttCallback
    );

    mqtt.setBufferSize(256);

    mqtt.setKeepAlive(30);

    mqtt.setSocketTimeout(10);

    // ----------------------------------------------------------
    // KẾT NỐI WIFI
    // ----------------------------------------------------------

    connectWiFi();

    // ----------------------------------------------------------
    // KẾT NỐI MQTT
    // ----------------------------------------------------------

    connectMQTT();

    // Đặt thời điểm để gửi mẫu cảm biến ngay sau khi khởi động.
    lastSensorSend =
        millis() - SENSOR_INTERVAL_MS;

    Serial.println();

    Serial.println(
        "ESP32-C3 da khoi dong thanh cong."
    );

    Serial.println(
        "Dang cho lenh dieu khien va gui du lieu."
    );
}

// ==============================================================================
// 11. VÒNG LẶP CHÍNH
// ==============================================================================

void loop() {

    // ----------------------------------------------------------
    // KIỂM TRA WIFI
    // ----------------------------------------------------------

    if (WiFi.status() != WL_CONNECTED) {

        Serial.println(
            "Wi-Fi bi mat ket noi."
        );

        connectWiFi();
    }

    // ----------------------------------------------------------
    // KIỂM TRA MQTT
    // ----------------------------------------------------------

    if (!mqtt.connected()) {

        Serial.println(
            "MQTT bi mat ket noi."
        );

        connectMQTT();
    }

    // ----------------------------------------------------------
    // DUY TRÌ MQTT VÀ NHẬN LỆNH
    // ----------------------------------------------------------

    mqtt.loop();

    // ----------------------------------------------------------
    // GỬI DỮ LIỆU CẢM BIẾN THEO CHU KỲ
    // ----------------------------------------------------------

    unsigned long now = millis();

    if (
        now - lastSensorSend >= SENSOR_INTERVAL_MS
    ) {

        lastSensorSend = now;

        readAndPublishSensors();
    }
}
