#pragma once

// --- GPIO pins ---
const int GPIO_SATA = 5;
const int GPIO_CHASSIS = 4;
const int GPIO_ATX = 3;
const int GPIO_SEED = 2;

// --- Hardware Logic Level ---
// The logic level of the SATA power pin when the PC is ON.
#define PC_ON LOW
// The logic level of the CHASSIS pin when the case is OPENED.
#define CHASSIS_OPEN HIGH

// --- Timing & Interval Settings ---
const int LOOP_DELAY_MS = 50;
const int REPORT_INTERVAL_SECONDS = 60;
const int WIFI_RECONNECT_INTERVAL_SECONDS = 5;

// Duration of the pulse to simulate a power button press.
const int ATX_POWER_TRIGGER_PULSE_DURATION_MS = 500;
// Delay to wait for the system to enter sleep/shutdown after a power trigger.
const int POST_ATX_POWER_TRIGGER_COOLDOWN_SECONDS = 10;

// --- Calculated Constants ---
const int REPORT_LOOP_COUNT_MAX = (REPORT_INTERVAL_SECONDS * 1000) / LOOP_DELAY_MS;
const int WIFI_RECONNECT_LOOP_COUNT_MAX = (WIFI_RECONNECT_INTERVAL_SECONDS * 1000) / LOOP_DELAY_MS;