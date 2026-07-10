/*
 * Project Name: ESP32 Hyundai CAN Bus Multi-Gauge.
 * Target Vehicle: Hyundai Tiburon / Coupe (ECU SIMK43 / SIMK4x).
 * Hardware: ESP32 (NodeMCU-32s), GC9A01 (Circular LCD), SN65HVD230 (CAN Transceiver).
 * Description: Passive CAN bus sniffer and sensor fusion gauge.
 */
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include "driver/twai.h"
#include "ui.h"
#include "logic.h"

// --- HARDWARE CONFIGURATION ---
// CAN Pins: GPIO 16 and 17 are the default UART2 pins on the ESP32, 
// but they can be re-mapped to any other GPIOs if needed.
#define CAN_TX_PIN      GPIO_NUM_17 // Physical CAN TX pin
#define CAN_RX_PIN      GPIO_NUM_16 // Physical CAN RX pin
#define VOLT_PIN        GPIO_NUM_34 // Voltage Divider (100k/20k -> 1:6 Ratio)
#define ILL_PIN         GPIO_NUM_13 // Illumination Pin (High = Night)

// --- CONFIGURATION ---
// Set DEBUG_CAN_SNIFFER to true to enable raw CAN frame logging to the Serial Monitor.
// Useful for reverse-engineering new frames or troubleshooting connection issues.
#define DEBUG_CAN_SNIFFER false

// Voltage divider: 100k / 20k = 6:1. VOLT_CALIBRATION is a final
// correction factor to be adjusted against a trusted multimeter.
#define VOLT_DIVIDER_RATIO 6.0f
#define VOLT_CALIBRATION   1.0f

// Data freshness and task scheduling
#define SPEED_TIMEOUT_MS          100UL
#define TEMP_TIMEOUT_MS           500UL
#define FUEL_TIMEOUT_MS           100UL
#define CAN_VOLTAGE_TIMEOUT_MS    100UL
#define MAX_FUEL_FRAME_GAP_MS     100UL
#define ADC_READ_INTERVAL_MS       20UL
#define UI_UPDATE_INTERVAL_MS      33UL
#define CAN_STATUS_INTERVAL_MS   1000UL
#define ILL_DEBOUNCE_MS            75UL

#define MIN_VALID_VOLTAGE 5.0f
#define MAX_VALID_VOLTAGE 18.5f

// --- SCREEN CONFIG ---
#define INVERT_COLORS   true
#define SWAP_BYTES      true 
static const uint16_t screenWidth  = 240;
static const uint16_t screenHeight = 240;
#define BUFFER_SIZE     (screenWidth * 60)

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[BUFFER_SIZE];

TFT_eSPI tft = TFT_eSPI();

// --- GLOBAL VARIABLES ---
// recentFuel: distance-weighted L/100km over the most recent 1 km.
// Fuel consumed at idle is included and raises the city-driving result.
RecentFuelConsumption recentFuel;

// Engine data
int current_speed = 0;          // km/h
int current_coolant_temp = 0;   // deg C
float current_voltage = 0.0f;   // Volts from calibrated ADC
float can_voltage = 0.0f;       // Volts reported by ECU
bool adc_voltage_valid = false;
bool is_night_mode = false;

// CAN data validity
bool can_driver_installed = false;
bool can_driver_ready = false;
bool has_speed_data = false;
bool has_temp_data = false;
bool has_fuel_data = false;
bool has_can_voltage_data = false;

unsigned long last_speed_frame = 0;
unsigned long last_temp_frame = 0;
unsigned long last_fuel_frame = 0;
unsigned long last_can_voltage_frame = 0;

// Fuel frame timing (ID 0x545)
unsigned long prev_fuel_time = 0;
bool first_fuel_packet = true;

// Scheduling and diagnostics
unsigned long last_adc_read = 0;
unsigned long last_ui_update = 0;
unsigned long last_can_status_check = 0;
uint32_t last_reported_rx_missed = 0;
uint32_t last_reported_rx_overrun = 0;

static bool data_is_fresh(bool available, unsigned long last_update,
                          unsigned long timeout_ms, unsigned long now) {
    return available && ((unsigned long)(now - last_update) <= timeout_ms);
}

static void invalidate_fuel_calculation() {
    has_fuel_data = false;
    first_fuel_packet = true;
    recentFuel.reset();
}

static void invalidate_can_data() {
    has_speed_data = false;
    has_temp_data = false;
    has_can_voltage_data = false;
    invalidate_fuel_calculation();
}

/* --- DISPLAY DRIVER --- */
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)&color_p->full, w * h, SWAP_BYTES);
    tft.endWrite();

    lv_disp_flush_ready(disp);
}

/* --- VOLTMETER (CALIBRATED ADC) --- */
void handle_voltage() {
    uint32_t millivolt_sum = 0;

    // Average calibrated millivolt readings to reduce electrical noise.
    for (int i = 0; i < 16; i++) {
        millivolt_sum += analogReadMilliVolts(VOLT_PIN);
    }

    float pin_voltage = ((float)millivolt_sum / 16.0f) / 1000.0f;
    float measured_voltage =
        pin_voltage * VOLT_DIVIDER_RATIO * VOLT_CALIBRATION;

    if (measured_voltage >= MIN_VALID_VOLTAGE &&
        measured_voltage <= MAX_VALID_VOLTAGE) {
        if (!adc_voltage_valid) {
            current_voltage = measured_voltage;
        } else {
            // Low-pass filter for a stable displayed value.
            current_voltage =
                (current_voltage * 0.9f) + (measured_voltage * 0.1f);
        }
        adc_voltage_valid = true;
    } else {
        adc_voltage_valid = false;
    }
}

/* --- NIGHT MODE HANDLER --- */
void read_night_mode(unsigned long now) {
    static bool candidate_state = false;
    static unsigned long candidate_since = 0;

    bool raw_state = digitalRead(ILL_PIN) == HIGH;

    if (raw_state != candidate_state) {
        candidate_state = raw_state;
        candidate_since = now;
    } else if (candidate_state != is_night_mode &&
               (unsigned long)(now - candidate_since) >= ILL_DEBOUNCE_MS) {
        is_night_mode = candidate_state;
    }
}

/* --- CAN BUS HANDLER --- */
void processCAN() {
    twai_message_t message;

    // Drain the receive queue before performing display work.
    while (twai_receive(&message, 0) == ESP_OK) {

#if DEBUG_CAN_SNIFFER
        if (!message.rtr) {
            Serial.printf("CAN RX | ID: 0x%X | DLC: %d | DATA: ",
                          message.identifier, message.data_length_code);
            for (int i = 0; i < message.data_length_code; i++) {
                Serial.printf("%02X ", message.data[i]);
            }
            Serial.println();
        }
#endif

        // Remote and extended frames do not contain the expected SIMK4x data.
        if (message.rtr || message.extd) {
            continue;
        }

        unsigned long frame_time = millis();

        switch (message.identifier) {

            // --- FRAME: DME1 (Speed & RPM) ---
            case 0x316:
                if (message.data_length_code > 6) {
                    current_speed = (int)message.data[6];
                    last_speed_frame = frame_time;
                    has_speed_data = true;
                }
                break;

            // --- FRAME: DME2 (Coolant Temp) ---
            case 0x329:
                if (message.data_length_code > 1) {
                    current_coolant_temp =
                        (int)((message.data[1] * 0.75f) - 48.0f);
                    last_temp_frame = frame_time;
                    has_temp_data = true;
                }
                break;

            // --- FRAME: DME4 (Fuel Consumption & Voltage) ---
            case 0x545:
                if (message.data_length_code >= 4) {
                    can_voltage = (float)message.data[3] * 0.1f;
                    last_can_voltage_frame = frame_time;
                    has_can_voltage_data = true;

                    uint16_t delta_raw =
                        ((uint16_t)message.data[2] << 8) | message.data[1];

                    last_fuel_frame = frame_time;
                    has_fuel_data = true;

                    if (!first_fuel_packet) {
                        unsigned long delta_time_ms =
                            frame_time - prev_fuel_time;

                        bool speed_fresh =
                            data_is_fresh(has_speed_data, last_speed_frame,
                                          SPEED_TIMEOUT_MS, frame_time);

                        // Reject long gaps. Missing FCO frames cannot be
                        // reconstructed and stale speed must not add distance.
                        if (delta_time_ms > 0 &&
                            delta_time_ms <= MAX_FUEL_FRAME_GAP_MS &&
                            speed_fresh) {
                            double fuel_l =
                                ((double)delta_raw * 0.128) / 1000000.0;
                            double distance_km =
                                ((double)current_speed *
                                 (double)delta_time_ms) / 3600000.0;

                            recentFuel.add(fuel_l, distance_km);
                        } else if (delta_time_ms > MAX_FUEL_FRAME_GAP_MS ||
                                   !speed_fresh) {
                            // The missing interval cannot be reconstructed.
                            invalidate_fuel_calculation();
                        }
                    } else {
                        first_fuel_packet = false;
                    }

                    prev_fuel_time = frame_time;
                }
                break;
        }
    }
}

void handle_can_health(unsigned long now) {
    if (!can_driver_installed) return;

    uint32_t alerts = 0;
    while (twai_read_alerts(&alerts, 0) == ESP_OK) {
        if (alerts & TWAI_ALERT_RX_QUEUE_FULL) {
            Serial.println("WARNING: TWAI RX queue full; CAN frames were lost.");
            invalidate_fuel_calculation();
        }

        if (alerts & TWAI_ALERT_BUS_OFF) {
            Serial.println("ERROR: TWAI bus-off; starting recovery.");
            can_driver_ready = false;
            invalidate_can_data();

            if (twai_initiate_recovery() != ESP_OK) {
                Serial.println("ERROR: Failed to initiate TWAI recovery.");
            }
        }

        if (alerts & TWAI_ALERT_BUS_RECOVERED) {
            Serial.println("TWAI bus recovered; restarting driver.");
            if (twai_start() == ESP_OK) {
                can_driver_ready = true;
                first_fuel_packet = true;
            } else {
                Serial.println("ERROR: Failed to restart TWAI driver.");
            }
        }

        if (alerts & (TWAI_ALERT_ABOVE_ERR_WARN | TWAI_ALERT_ERR_PASS)) {
            Serial.println("WARNING: Elevated TWAI error state.");
        }
    }

    if ((unsigned long)(now - last_can_status_check) >=
        CAN_STATUS_INTERVAL_MS) {
        last_can_status_check = now;

        twai_status_info_t status;
        if (twai_get_status_info(&status) == ESP_OK) {
            if (status.rx_missed_count != last_reported_rx_missed ||
                status.rx_overrun_count != last_reported_rx_overrun) {
                Serial.printf(
                    "WARNING: CAN loss counters: missed=%u, overrun=%u\n",
                    status.rx_missed_count, status.rx_overrun_count);
                last_reported_rx_missed = status.rx_missed_count;
                last_reported_rx_overrun = status.rx_overrun_count;
                invalidate_fuel_calculation();
            }
        }
    }
}

/* --- MAIN SETUP --- */
void setup() {
    Serial.begin(115200);

    // --- 1. CAN BUS INITIALIZATION (PASSIVE LISTEN-ONLY) ---
    twai_general_config_t g_config =
        TWAI_GENERAL_CONFIG_DEFAULT(
            CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_LISTEN_ONLY);
    g_config.rx_queue_len = 64;
    g_config.tx_queue_len = 0;
    g_config.alerts_enabled =
        TWAI_ALERT_RX_QUEUE_FULL |
        TWAI_ALERT_BUS_OFF |
        TWAI_ALERT_BUS_RECOVERED |
        TWAI_ALERT_ABOVE_ERR_WARN |
        TWAI_ALERT_ERR_PASS;

    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
        can_driver_installed = true;
        Serial.println("CAN Driver Installed Successfully.");

        if (twai_start() == ESP_OK) {
            can_driver_ready = true;
            Serial.println("CAN Driver Started in listen-only mode.");
        } else {
            Serial.println("CRITICAL: Failed to START CAN Driver.");
        }
    } else {
        Serial.println("CRITICAL: Failed to INSTALL CAN Driver.");
    }

    // ILL input uses a pulldown and software debounce.
    pinMode(ILL_PIN, INPUT_PULLDOWN);

    // ADC Config
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db); // 0-3.6V Range

    // Screen Init
    tft.begin();
    tft.setRotation(2);
    tft.invertDisplay(INVERT_COLORS);
    tft.fillScreen(TFT_BLACK);

    // LVGL Init
    lv_init();
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, BUFFER_SIZE);
    
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = screenWidth;
    disp_drv.ver_res = screenHeight;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    // UI Init
    ui_init();
}

/* --- MAIN LOOP --- */
void loop() {
    // CAN is serviced first so display rendering cannot starve the RX queue.
    processCAN();

    // Refresh time after queue processing because received frame timestamps
    // can be newer than the time at which the loop iteration started.
    unsigned long now = millis();
    handle_can_health(now);

    if ((unsigned long)(now - last_adc_read) >= ADC_READ_INTERVAL_MS) {
        last_adc_read = now;
        handle_voltage();
    }

    read_night_mode(now);

    if ((unsigned long)(now - last_ui_update) >= UI_UPDATE_INTERVAL_MS) {
        last_ui_update = now;

        bool speed_valid =
            can_driver_ready &&
            data_is_fresh(has_speed_data, last_speed_frame,
                          SPEED_TIMEOUT_MS, now);
        bool temp_valid =
            can_driver_ready &&
            data_is_fresh(has_temp_data, last_temp_frame,
                          TEMP_TIMEOUT_MS, now);
        bool fuel_frames_valid =
            can_driver_ready &&
            data_is_fresh(has_fuel_data, last_fuel_frame,
                          FUEL_TIMEOUT_MS, now);
        bool can_voltage_valid =
            can_driver_ready &&
            data_is_fresh(has_can_voltage_data, last_can_voltage_frame,
                          CAN_VOLTAGE_TIMEOUT_MS, now) &&
            can_voltage >= MIN_VALID_VOLTAGE &&
            can_voltage <= MAX_VALID_VOLTAGE;

        float display_voltage = 0.0f;
        bool display_voltage_valid = false;

        if (adc_voltage_valid && can_voltage_valid) {
            display_voltage =
                (current_voltage * 0.7f) + (can_voltage * 0.3f);
            display_voltage_valid = true;
        } else if (adc_voltage_valid) {
            display_voltage = current_voltage;
            display_voltage_valid = true;
        } else if (can_voltage_valid) {
            display_voltage = can_voltage;
            display_voltage_valid = true;
        }

        float recent_fuel = recentFuel.getAverageL100km();
        bool fuel_valid =
            speed_valid && fuel_frames_valid && recent_fuel >= 0.0f;

        update_engine_status(current_coolant_temp, temp_valid,
                             recent_fuel, fuel_valid, is_night_mode);
        update_gauge_logic(display_voltage, display_voltage_valid,
                           is_night_mode);
    }

    // LVGL uses millis() as its custom tick source.
    lv_timer_handler();
    delay(1);
}