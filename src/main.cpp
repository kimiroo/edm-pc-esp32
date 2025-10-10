#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "cert.h"
#include "config.h"

WiFiClientSecure client;
HTTPClient http;

const int GPIO_SATA = 5;
const int GPIO_CHASSIS = 4;
const int GPIO_ATX = 3;
const int GPIO_SEED = 2;

bool isInitLoop = true;
bool isReportRequired = false;
int wifiTimeoutCount = 0;
int reportLoopCount = 0;

const int LOOP_DELAY_MS = 100;
const int REPORT_INTERVAL_SECONDS = 60;
const int WIFI_RECONNECT_INTERVAL_SECONDS = 5;
const int REPORT_LOOP_COUNT_MAX = (REPORT_INTERVAL_SECONDS * 1000) / LOOP_DELAY_MS;
const int WIFI_RECONNECT_LOOP_COUNT_MAX = (WIFI_RECONNECT_INTERVAL_SECONDS * 1000) / LOOP_DELAY_MS;

char* sessionId;

void connectWiFi() {
    while (true) {
        Serial.print("Connecting to ");
        Serial.print(SSID);

        WiFi.setHostname(HOSTNAME);

        WiFi.begin(SSID, PASSWORD);

        int wifi_tries = 0;
        while (WiFi.status() != WL_CONNECTED && wifi_tries <= 20) {
            wifi_tries += 1;
            delay(500);
            Serial.print(".");
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("");
            Serial.print("Connected to ");
            Serial.print(SSID);
            Serial.println(".");

            Serial.println("IP address: ");
            Serial.println(WiFi.localIP());
            break;
        } else {
            Serial.println("Connection timed out. Retrying...");
        }
    }
}

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
    sessionId = new char[33]; // Allocate memory on the heap for the session ID
    generateRandomString(sessionId, 32);
    Serial.println("done.");

    // Set Root CA certificate
    Serial.print("Setting Root CA certificate...");
    client.setCACert(ROOT_CA);
    Serial.println("done.");

    Serial.println("Initialization complete.\n");
}

void loop() {
    // Connect to Wi-Fi if not
    if (WiFi.status() != WL_CONNECTED) {
        // If disconnected, attempt to reconnect asynchronously at regular intervals
        if (wifiTimeoutCount == 0) {
            if (!isInitLoop) {
                Serial.println("Wi-Fi connection lost. Attempting to reconnect...");
            }
            Serial.printf("Connecting to '%s'...\n", SSID);
            WiFi.setHostname(HOSTNAME);
            WiFi.begin(SSID, PASSWORD);
        }
        // Trigger the reconnect attempt every 50 loops
        wifiTimeoutCount = (wifiTimeoutCount + 1) % WIFI_RECONNECT_LOOP_COUNT_MAX;
    } else {
        // If the connection is stable, reset the reconnect attempt counter
        wifiTimeoutCount = 0;
    }

    // Get current state
    int sataState = digitalRead(GPIO_SATA);
    int chassisState = digitalRead(GPIO_CHASSIS);
    isReportRequired = sataState == HIGH || chassisState == HIGH;

    if (sataState == LOW) {
        Serial.print("SATA: LOW ; ");
    } else {
        Serial.print("SATA: HIGH; ");
    }

    if (chassisState == LOW) {
        Serial.println("CHASSIS: LOW ; ATX: LOW");
    } else {
        Serial.println("CHASSIS: HIGH; ATX: HIGH");
    }

    // Report to the server
    if (reportLoopCount == 0 || isReportRequired) {
        if (WiFi.status() == WL_CONNECTED) {
            JsonDocument apiResponse;
            reportToServer(sataState == HIGH, chassisState == HIGH, apiResponse);
            if (apiResponse["isSuccessful"].as<bool>()) {
                //
            }
        }
    }
    reportLoopCount = (reportLoopCount + 1) % REPORT_LOOP_COUNT_MAX;

    digitalWrite(GPIO_ATX, chassisState);

    delay(LOOP_DELAY_MS);
}