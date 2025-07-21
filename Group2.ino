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

int count = 0;
RTC_DS3231 rtc;

#define WIFI_SSID "Weekends"
#define WIFI_PASS ""

const int chipSelect = 5;
#define apiUrl "https://iot.online.ng/api/device5.php"

int Sampling = 0;
File dataFile;
bool wasWiFiConnected = false;

bool prevIR1State = HIGH;
bool prevIR2State = HIGH;

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("System Initialized");

    Serial.print("Connecting to WiFi...");
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    unsigned long startAttemptTime = millis();
    const unsigned long timeout = 3000;

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

    pinMode(IRIN_SENSOR_PIN, INPUT_PULLUP);
    pinMode(IROUT_SENSOR_PIN, INPUT_PULLUP);
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
        while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 1000) {
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
    int currIR1 = digitalRead(IRIN_SENSOR_PIN);
    int currIR2 = digitalRead(IROUT_SENSOR_PIN);
    String applianceS = (count > 0) ? "Light ON" : "Light OFF";

    char dateStr[11], timeStr[9];
    snprintf(dateStr, sizeof(dateStr), "%04d-%02d-%02d", now.year(), now.month(), now.day());
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", now.hour(), now.minute(), now.second());

    if (currIR1 == LOW && prevIR1State == HIGH) {
        delay(100); // debounce
        if (digitalRead(IRIN_SENSOR_PIN) == LOW) {
            count++;
            digitalWrite(LED1, HIGH);
            Serial.println("➡️ Entry Detected!");
            Serial.printf("📅 Date: %s | ⏰ Time: %s\n", dateStr, timeStr);
            Serial.print("Count: "); Serial.println(count);

            if (count > 0) digitalWrite(RELAY_PIN, HIGH);
            applianceS = "Light ON";

            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Entry: ");
            lcd.print(count);
            delay(1000);
        }
    }

    if (currIR2 == LOW && prevIR2State == HIGH) {
        delay(100); // debounce
        if (digitalRead(IROUT_SENSOR_PIN) == LOW) {
            count--;
            if (count < 0) count = 0;
            digitalWrite(LED2, HIGH);
            Serial.println("⬅️ Exit Detected!");
            Serial.printf("📅 Date: %s | ⏰ Time: %s\n", dateStr, timeStr);
            Serial.print("Count: "); Serial.println(count);

            if (count == 0) digitalWrite(RELAY_PIN, LOW);
            applianceS = (count > 0) ? "Light ON" : "Light OFF";

            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Exit: ");
            lcd.print(count);
            delay(1000);
        }
    }

    if (currIR1 == HIGH) digitalWrite(LED1, LOW);
    if (currIR2 == HIGH) digitalWrite(LED2, LOW);

    // Send or log data
    sendSensorData(applianceS, count, 0);

    delay(100);  // loop pace delay

    // 🟢 FIX: Move state tracking update here to properly register next falling edge
    prevIR1State = currIR1;
    prevIR2State = currIR2;
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

        Serial.println("[DEBUG] Sending payload: " + payload);
        int httpCode = http.POST(payload);

        if (httpCode > 0 && httpCode == 200) {
            Serial.printf("[DEBUG] HTTP %d: %s\n", httpCode, http.getString().c_str());
        } else {
            Serial.printf("[ERROR] HTTP failed: %s\n", http.errorToString(httpCode).c_str());
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
        Serial.println("[SD] Data logged.");
    } else {
        Serial.println("❌ SD write failed.");
    }
}

void sendStoredData() {
    dataFile = SD.open("/SENSOR_DATA.txt", FILE_READ);
    if (!dataFile) {
        Serial.println("❌ Failed to open log file.");
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

        Serial.println("[SD] Sending: " + line);
        bool success = sendLineToServer(line);

        if (!success) {
            remainingLines.push_back(line);
            Serial.println("[SD] Failed to send, keeping.");
        }

        delay(300);
    }
    dataFile.close();

    dataFile = SD.open("/SENSOR_DATA.txt", FILE_WRITE);
    if (dataFile) {
        for (String& l : remainingLines) {
            dataFile.println(l);
        }
        dataFile.close();
        Serial.println("[SD] Log updated.");
    } else {
        Serial.println("❌ Failed to write updated log.");
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
        Serial.println("[ERROR] Malformed line.");
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
    http.end();

    return (httpCode == 200);
}
