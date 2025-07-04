
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>

LiquidCrystal_I2C lcd(0x27, 16, 2); // 16x2 LCD with I2C

#define IRIN_SENSOR_PIN  26  // GPIO pin where the IR sensor is connected
#define IROUT_SENSOR_PIN 13  // Second IR sensor pin
#define LED1 32              // GPIO pin for motor or LED
#define LED2 21 
#define RELAY_PIN 33         // GPIO pin for the relay

int count = 0;               // Tracks the number of valid elements
RTC_DS3231 rtc;

// WiFi Credentials
#define WIFI_SSID "Weekends"
#define WIFI_PASS ""

// SD Card
const int chipSelect = 5;  // CS pin for SD card on ESP32

// API URL
#define apiUrl "https://iot.online.ng/api/device5.php"

// Variables
int Sampling = 0;
File dataFile;
bool wasWiFiConnected = false;

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("System Initialized");

    Serial.print("Connecting to WiFi...");
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    unsigned long startAttemptTime = millis();
    const unsigned long timeout = 30000;

    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < timeout) {
        Serial.print(".");
        delay(500);
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("✅ Connected!");
        wasWiFiConnected = true;
    } else {
        Serial.println("❌ WiFi connection failed.");
        wasWiFiConnected = false;
    }

    pinMode(IRIN_SENSOR_PIN, INPUT);
    pinMode(IROUT_SENSOR_PIN, INPUT);
    pinMode(LED1, OUTPUT);
    pinMode(LED2, OUTPUT);
    pinMode(RELAY_PIN, OUTPUT);

    Wire.begin(14, 27);  // SDA = 14, SCL = 27

    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("ESP32 Initialized");

    if (!rtc.begin()) {
        Serial.println("❌ RTC Module not detected!");
    } else {
        Serial.println("✅ RTC Connected!");
        if (rtc.lostPower()) {
            Serial.println("⚠️ RTC power failure, resetting the time!");
            rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
        }
    }

    if (!SD.begin(chipSelect)) {
        Serial.println("❌ SD Card initialization failed!");
        lcd.setCursor(0, 1);
        lcd.print("SD Init Fail!");
        while (1);
    } else {
        Serial.println("✅ SD Card Initialized!");
        lcd.setCursor(0, 1);
        lcd.print("SD Init OK");
    }

    if (!SD.exists("/SENSOR_DATA.txt")) {
        dataFile = SD.open("/SENSOR_DATA.txt", FILE_WRITE);
        if (dataFile) {
            dataFile.println("Counter\tDate\tTime\tappliances\tentry_count\texit_count");
            dataFile.close();
        }
    }

    digitalWrite(LED1, LOW);
    digitalWrite(LED2, LOW);
    digitalWrite(RELAY_PIN, LOW);

    delay(2000);
    lcd.clear();
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        unsigned long startAttemptTime = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
            delay(500);
        }
    }

    bool isCurrentlyConnected = (WiFi.status() == WL_CONNECTED);
    if (isCurrentlyConnected && !wasWiFiConnected) {
        Serial.println("✅ WiFi restored. Sending stored SD card data...");
        sendStoredData();
    }

    wasWiFiConnected = isCurrentlyConnected;

    DateTime now = rtc.now();
    int sensorValue = digitalRead(IRIN_SENSOR_PIN);
    int sensor2Value = digitalRead(IROUT_SENSOR_PIN);
    String applianceS = "Light OFF";

    Serial.print("IR Sensor 1: ");
    Serial.print(sensorValue);
    Serial.print(" | IR Sensor 2: ");
    Serial.println(sensor2Value);

    char dateStr[11], timeStr[9];
    snprintf(dateStr, sizeof(dateStr), "%04d-%02d-%02d", now.year(), now.month(), now.day());
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", now.hour(), now.minute(), now.second());

    if (sensorValue == LOW) {
        Serial.println("Movement detected! From IR1");
        count++;
        digitalWrite(LED1, HIGH);
        Serial.printf("📅 Date: %s | ⏰ Time: %s\n", dateStr, timeStr);
        Serial.print("Count after increment: ");
        Serial.println(count);

        if (count > 0) digitalWrite(RELAY_PIN, HIGH), applianceS = "Light ON" ;
        else digitalWrite(RELAY_PIN, LOW), applianceS = "Light OFF";

        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Entry:");
        lcd.print(count);
        delay(2000);
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Total Entry: ");
        lcd.print(count);
        delay(2000);
        lcd.clear();
    } else if (sensor2Value == LOW) {
        Serial.println("Movement detected! From IR2");
        count--;
        digitalWrite(LED2, HIGH);
        Serial.printf("📅 Date: %s | ⏰ Time: %s\n", dateStr, timeStr);
        Serial.print("Count after decrement: ");
        Serial.println(count);

        if (count > 0) digitalWrite(RELAY_PIN, HIGH);
        else digitalWrite(RELAY_PIN, LOW);

        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Exit - 1");
        delay(2000);
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Total Entry: ");
        lcd.print(count);
        delay(2000);
        lcd.clear();
    } else {
        Serial.println("No movement.");
        digitalWrite(LED1, LOW);
        digitalWrite(LED2, LOW);
    }

    // Always send or log this new data
    sendSensorData(applianceS, count, 0);

    delay(500);
}

void sendSensorData(String appliances, int entryCount, int exitCount) {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(apiUrl);
        http.addHeader("Content-Type", "application/json");

        DynamicJsonDocument doc(512);
        doc["appliances"] = appliances;
        doc["entry_count"] = entryCount;
        doc["exit_count"] = exitCount;

        String payload;
        serializeJson(doc, payload);

        Serial.println("[DEBUG] Sending sensor payload: " + payload);
        int httpCode = http.POST(payload);

        if (httpCode > 0 && httpCode == 200) {
            Serial.printf("[DEBUG] Sensor Data HTTP code: %d\n", httpCode);
            Serial.println("[DEBUG] Response: " + http.getString());
        } else {
            Serial.printf("[ERROR] Sensor HTTP failed: %s\n", http.errorToString(httpCode).c_str());
            logSensorDataToSD(appliances, entryCount, exitCount);
        }

        http.end();
    } else {
        logSensorDataToSD(appliances, entryCount, exitCount);
    }
}

void logSensorDataToSD(String appliances, int entryCount, int exitCount) {
    DateTime now = rtc.now();
    Sampling++;

    dataFile = SD.open("/SENSOR_DATA.txt", FILE_APPEND);
    if (dataFile) {
        dataFile.print(Sampling); dataFile.print("\t");
        dataFile.print(now.day()); dataFile.print('/');
        dataFile.print(now.month()); dataFile.print('/');
        dataFile.print(now.year()); dataFile.print("\t");
        dataFile.print(now.hour()); dataFile.print(':');
        dataFile.print(now.minute()); dataFile.print(':');
        dataFile.print(now.second()); dataFile.print("\t");
        dataFile.print(appliances); dataFile.print("\t");
        dataFile.print(entryCount); dataFile.print("\t");
        dataFile.println(exitCount);
        dataFile.close();
        Serial.println("[SD] Logged sensor data to SD card.");
    } else {
        Serial.println("❌ Failed to open SENSOR_DATA.txt for sensor logging.");
    }
}

void sendStoredData() {
    dataFile = SD.open("/SENSOR_DATA.txt", FILE_READ);
    if (!dataFile) {
        Serial.println("❌ Failed to open SENSOR_DATA.txt for reading.");
        return;
    }

    std::vector<String> remainingLines;
    bool isHeader = true;

    while (dataFile.available()) {
        String line = dataFile.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        if (isHeader) {
            remainingLines.push_back(line);
            isHeader = false;
            continue;
        }

        Serial.println("[SD] Trying to send line: " + line);
        bool success = sendLineToServer(line);

        if (!success) {
            remainingLines.push_back(line);
            Serial.println("[SD] Failed to send, keeping line.");
        } else {
            Serial.println("[SD] Line sent successfully.");
        }

        delay(500);
    }
    dataFile.close();

    dataFile = SD.open("/SENSOR_DATA.txt", FILE_WRITE);
    if (dataFile) {
        for (String& l : remainingLines) {
            dataFile.println(l);
        }
        dataFile.close();
        Serial.println("[SD] SENSOR_DATA.txt updated.");
    } else {
        Serial.println("❌ Failed to rewrite SENSOR_DATA.txt.");
    }
}

bool sendLineToServer(String line) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Sending Logged");
    lcd.setCursor(0, 1);
    lcd.print("Data...");

    DynamicJsonDocument doc(512);

    String parts[6];
    int index = 0;

    while (line.length() > 0 && index < 6) {
        int tabIndex = line.indexOf('\t');
        if (tabIndex == -1) {
            parts[index++] = line;
            break;
        } else {
            parts[index++] = line.substring(0, tabIndex);
            line = line.substring(tabIndex + 1);
        }
    }

    if (index < 6) {
        Serial.println("[ERROR] Line parsing error.");
        return false;
    }

    doc["appliances"] = parts[3];
    doc["entry_count"] = parts[4].toInt();
    doc["exit_count"] = parts[5].toInt();

    String payload;
    serializeJson(doc, payload);

    HTTPClient http;
    http.begin(apiUrl);
    http.addHeader("Content-Type", "application/json");

    int httpCode = http.POST(payload);

    bool success = false;
    if (httpCode > 0 && httpCode == 200) {
        Serial.printf("[DEBUG] Sent saved payload. HTTP: %d\n", httpCode);
        success = true;
    } else {
        Serial.printf("[ERROR] Failed to send saved line. HTTP: %s\n", http.errorToString(httpCode).c_str());
    }

    http.end();
    return success;
}
