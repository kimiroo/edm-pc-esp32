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
int reportLoopCount = 0;
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

void reportToServer(bool isAlive, bool isOpened, JsonDocument& result) {
    result.clear(); // Clear previous data

    Serial.printf("Requesting URL: %s\n", API_URL);
    if (http.begin(client, API_URL)) {
        http.addHeader("Content-Type", "application/json");
        http.addHeader("X-DARAK-API-Key", API_KEY);

        JsonDocument requestBody;
        requestBody["pcName"] = PC_NAME;
        requestBody["isAlive"] = isAlive;
        requestBody["isOpened"] = isOpened;
        requestBody["sessionID"] = sessionId;

        String requestBodyString;
        serializeJson(requestBody, requestBodyString);

        int httpCode = http.POST(requestBodyString);

        if (httpCode > 0) {
            JsonDocument responseBody;
            DeserializationError err = deserializeJson(responseBody, http.getStream());

            if (err) { // Failed
                Serial.print(F("deserializeJson() failed: "));
                Serial.println(err.c_str());
                result["isSuccessful"] = false;
            } else { // Success
                Serial.println("JSON response parsed successfully.");
                result["isSuccessful"] = true;
                result["responseBody"] = responseBody;
            }
        } else {
            Serial.printf("[HTTPS] POST... failed, error: %s\n", http.errorToString(httpCode).c_str());
        }
        http.end();
    } else {
        Serial.printf("[HTTPS] Unable to connect to %s\n", API_URL);
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("Waiting..."); // DEBUG
    delay(5000);                  // DEBUG
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
}

void loop() {
    // Connect to Wi-Fi if not
    if (WiFi.status() != WL_CONNECTED) {
        // If disconnected, attempt to reconnect asynchronously
        if (wifiTimeoutCount == 0) {
            Serial.println("Wi-Fi connection lost. Attempting to reconnect...");
            Serial.printf("Connecting to '%s'...\n", SSID);
            WiFi.setHostname(HOSTNAME);
            WiFi.begin(SSID, PASSWORD);
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

    if (chassisState == HIGH && !lastIsOpened) {
        isIsOpenedReported = false;
    }

    lastIsAlive = (sataState == PC_ON) || lastIsAlive;
    lastIsOpened = (chassisState == CHASSIS_OPEN) || lastIsOpened;

    isReportRequired = !isIsOpenedReported && (isReportRequired || lastIsOpened);

    // Report to the server
    if (reportLoopCount == 0 || isReportRequired) {
        if (WiFi.status() == WL_CONNECTED) {
            JsonDocument apiResponse;
            reportToServer(lastIsAlive, lastIsOpened, apiResponse);

            if (apiResponse["isSuccessful"].as<bool>()) {
                if (apiResponse["responseBody"]["turnOffPC"].as<bool>()) {
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
            }
        }
    }
    reportLoopCount = (reportLoopCount + 1) % REPORT_LOOP_COUNT_MAX;

    delay(LOOP_DELAY_MS);
}