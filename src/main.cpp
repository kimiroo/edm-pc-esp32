#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "headers.h"
#include "cert.h"
#include "config.h"

#if USE_HTTPS
WiFiClientSecure client;
#else
WiFiClient client;
#endif
HTTPClient http;

bool isReportRequired = false;
bool lastIsAlive = false;
bool lastIsOpened = false;
bool isIsOpenedReported = false;
// Initialize to 1 to prevent an immediate reconnection attempt on the first loop,
// as setup() already initiates it.
int wifiTimeoutCount = 1;
// Initialize to 1 to prevent an immediate report since takes few seconds to connect to the Wi-Fi after boot
int reportLoopCount = 1;
int wifiCooldownLoopCount = 0;
unsigned long loopCooldownUntil = 0;

char sessionId[33];

void generateRandomString(char* buffer, int length) {
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (int i = 0; i < length; i++) {
        int index = random(0, sizeof(charset) - 2); // -2 because sizeof includes the null terminator
        buffer[i] = charset[index];
    }
    buffer[length] = '\0'; // Null-terminate the string
}

bool reportToServer(bool isAlive, bool isOpened, JsonDocument& responseDoc) {
    responseDoc.clear();

    if (http.begin(client, API_URL)) {
        http.addHeader("Content-Type", "application/json");
        http.addHeader("X-DARAK-API-Access-Key", API_KEY);

        JsonDocument requestBody;
        requestBody["pcID"] = PC_ID;
        requestBody["pcName"] = PC_NAME;
        requestBody["isAlive"] = isAlive;
        requestBody["isOpened"] = isOpened;
        requestBody["sessionID"] = sessionId;

        String requestBodyString;
        serializeJson(requestBody, requestBodyString);

        int httpCode = http.POST(requestBodyString);

        if (httpCode > 0) {
            // HTTP request was successful. Check if response body exists.
            if (http.getStream().available() > 0) {
                // Check for errors in body
                DeserializationError err = deserializeJson(responseDoc, http.getStream());
                if (err) {
                    Serial.print(F("deserializeJson() failed: "));
                    Serial.println(err.c_str());
                    http.end();
                    return false; // Parsing JSON failed
                }

                http.end();
                return !responseDoc["turnOffPC"].isUnbound();
            }
            http.end();
            return true; // HTTP request success (Parsing JSON successful, or response body empty)
        } else {
            Serial.printf("POST request failed. error: %s\n", http.errorToString(httpCode).c_str());
        }
        http.end();
    } else {
        Serial.printf("Unable to connect to %s\n", API_URL);
    }

    return false; // Connection or POST request failed
}

void setup() {
    Serial.begin(115200);
    //Serial.println("Waiting..."); // DEBUG
    //delay(5000);                  // DEBUG
    Serial.println("Initializing...");

    // Initialize GPIO pins
    Serial.print("Initializing GPIO pins...");
    pinMode(GPIO_SATA, INPUT_PULLUP);
    pinMode(GPIO_CHASSIS, INPUT_PULLUP);
    pinMode(GPIO_ATX, OUTPUT);
    digitalWrite(GPIO_ATX, LOW);
    Serial.println("done.");

    // Generate Session ID
    Serial.print("Generating Session ID...");
    randomSeed(analogRead(GPIO_SEED));
    generateRandomString(sessionId, 32);
    Serial.println("done.");

    // Set Root CA certificate
    Serial.print("Setting Root CA certificate...");
#if USE_HTTPS
    client.setCACert(ROOT_CA);
    Serial.println("done.");
#else
    Serial.println("skipped (HTTP mode).");
#endif

    // Initial Wi-Fi connection attempt
    Serial.printf("Connecting to '%s'...", SSID);
    WiFi.setHostname(HOSTNAME);
    WiFi.begin(SSID, PASSWORD);
    Serial.println("done.");

    Serial.println("Initialization complete.\n");
    Serial.printf("PC Name: %s", PC_NAME);
    Serial.printf("Session ID: %s\n\n", sessionId);
}

void loop() {
    // Connect to Wi-Fi if not
    if (WiFi.status() != WL_CONNECTED && wifiCooldownLoopCount > INIT_WIFI_COOLDOWN_LOOP_COUNT_MAX) {
        // If disconnected, attempt to reconnect asynchronously
        if (wifiTimeoutCount == 0) {
            Serial.println("Wi-Fi connection lost. Attempting to reconnect...");
            Serial.printf("Connecting to '%s'...\n", SSID);
            WiFi.setHostname(HOSTNAME);
            WiFi.begin(SSID, PASSWORD);
        } else {
            Serial.printf("Waiting for the Wi-Fi to reconnect... (loop: %d)\n", wifiTimeoutCount);
        }
        wifiTimeoutCount = (wifiTimeoutCount + 1) % WIFI_RECONNECT_LOOP_COUNT_MAX;
    } else {
        // If the connection is stable, reset the reconnect attempt counter
        wifiTimeoutCount = 0;
    }

    if (loopCooldownUntil != 0 && (long)(millis() - loopCooldownUntil) < 0) {
        // In a cooldown period (for PC to fully change its power state), so skip main logic.
        delay(LOOP_DELAY_MS);
        return;
    }

    // Get current state
    int sataState = digitalRead(GPIO_SATA);
    int chassisState = digitalRead(GPIO_CHASSIS);

    if (chassisState == !CHASSIS_OPEN && isIsOpenedReported) {
        isIsOpenedReported = false;
    }

    lastIsAlive = (sataState == PC_ON) || lastIsAlive;
    lastIsOpened = (chassisState == CHASSIS_OPEN) || lastIsOpened;
    isReportRequired = !isIsOpenedReported && (isReportRequired || lastIsOpened);

    // Report to the server
    if (reportLoopCount == 0 || isReportRequired) {
        if (WiFi.status() == WL_CONNECTED) {
            JsonDocument apiResponse; // 서버 응답 본문을 담을 변수

            if (reportToServer(lastIsAlive, lastIsOpened, apiResponse)) {
                // reportToServer가 true를 반환하면, isReportRequired를 초기화

                if (apiResponse["turnOffPC"].as<bool>() == true) {
                    // Trigger ATX POWER
                    digitalWrite(GPIO_ATX, HIGH);
                    delay(ATX_POWER_TRIGGER_PULSE_DURATION_MS);
                    digitalWrite(GPIO_ATX, LOW);

                    // Set cooldown time to let PC fully change its power state
                    loopCooldownUntil = millis() + (POST_ATX_POWER_TRIGGER_COOLDOWN_SECONDS * 1000);
                    Serial.printf("Power triggered. Entering cooldown for %d seconds.\n", POST_ATX_POWER_TRIGGER_COOLDOWN_SECONDS);
                }

                lastIsAlive = false;
                lastIsOpened = false;
                isReportRequired = false;
                isIsOpenedReported = true;
            } else {
                Serial.println("Report to server failed.");
            }
        } else {
            Serial.println("Not connected to Wi-Fi. Not reporting...");
        }
    }

    reportLoopCount = (reportLoopCount + 1) % REPORT_LOOP_COUNT_MAX;

    if (wifiCooldownLoopCount <= INIT_WIFI_COOLDOWN_LOOP_COUNT_MAX) {
        wifiCooldownLoopCount += 1;
    }

    delay(LOOP_DELAY_MS);
}