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
#define CAN_BAUD_RATE   500000      // 500 kbps

// VOLT_CALIBRATION: Adjustment factor for the hardware voltage divider.
// Represents the ratio of the resistor network. Tweak this value if the displayed voltage differs from a multimeter reading.
#define VOLT_CALIBRATION 6.532f

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
// fuelAvg: RollingAverage object used to smooth out the L/100km fuel consumption calculations over time.
RollingAverage fuelAvg;

// Engine Data
int current_speed = 0;          // km/h
int current_coolant_temp = 0;   // °C
// current_voltage: Processed raw ADC reading from the hardware voltage divider.
float current_voltage = 12.0f;  // Volts
// can_voltage: Voltage value reported directly by the ECU over the CAN bus.
float can_voltage = 0.0f;       // Volts (from ECU)
float current_lph = 0.0f;       // Liters per hour (L/h) - updated continuously
bool is_night_mode = false;     // Night Mode Status

// Fuel Calculation Variables (ID 0x545)
// These act as buffers for the "Time Window Accumulation Algorithm"
uint16_t prev_fuel_ul = 0;
unsigned long prev_fuel_time = 0;
bool first_fuel_packet = true;

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

/* --- VOLTMETER (REAL ADC) --- */
void handle_voltage() {
    long adc_sum = 0;
    // 16x Oversampling technique:
    // Taking 16 rapid readings and averaging them reduces electrical noise on the ESP32's ADC,
    // resulting in a much more stable voltage measurement.
    for (int i=0; i<16; i++) {
        adc_sum += analogRead(VOLT_PIN);
    }
    float adc_avg = (float)adc_sum / 16.0f;

    // Convert to Volts: (ADC / MaxADC) * Vref * Divider
    // ESP32 ADC is 12-bit (0-4095)
    float measured_volts = (adc_avg / 4095.0f) * 3.3f * VOLT_CALIBRATION;

    // Smoothing (simple Low Pass Filter)
    current_voltage = (current_voltage * 0.9f) + (measured_volts * 0.1f);
}

/* --- NIGHT MODE HANDLER --- */
void read_night_mode() {
    // Read GPIO 13 (Assuming HIGH = Lights ON / Night)
    // Debouncing not strictly needed for light switch, but good practice to filter glitches
    // For now simple read is enough.
    is_night_mode = digitalRead(ILL_PIN);
}

/* --- CAN BUS HANDLER --- */
void processCAN() {
    twai_message_t message;

    // Pętla 'while' wyciąga z bufora wszystko co przyszło, póki bufor nie będzie pusty.
    while (twai_receive(&message, 0) == ESP_OK) {
        
#if DEBUG_CAN_SNIFFER
        // --- SEKCJA SNIFFERA (Tylko logowanie tekstu) ---
        if (!message.rtr) {
            Serial.printf("CAN RX | ID: 0x%X | DLC: %d | DATA: ", message.identifier, message.data_length_code);
            for (int i = 0; i < message.data_length_code; i++) {
                Serial.printf("%02X ", message.data[i]);
            }
            Serial.println();
        }
#endif // Koniec bloku warunkowego dla sniffera!

        // --- PRODUCTION LOGIC (Always active) ---
        // This code executes regardless of whether the sniffer is enabled or not.
        switch (message.identifier) {
            
            // --- FRAME: DME1 (Speed & RPM) ---
            // Note: Byte 6 contains the vehicle speed, while Bytes 2-3 contain the engine RPM.
            // RPM is currently extracted here but not actively used in the UI logic.
            case 0x316: 
                if (message.data_length_code > 6) {
                    current_speed = (int)message.data[6]; // Byte 6 is VS_CAN in km/h
                }
                break;

            // --- FRAME: DME2 (Coolant Temp) ---
            case 0x329: 
                if (message.data_length_code > 1) {
                    // --- TEMPERATURE CALIBRATION (Offset adjustment) ---
                    // Base CAN reading formula: (HEX * 0.75) - 48.0
                    // If the screen reading differs from an OBD2 scanner:
                    // - To INCREASE the displayed temp by e.g. 2 degrees, change the offset from -48.0f to -46.0f
                    // - To DECREASE the displayed temp by e.g. 3 degrees, change the offset from -48.0f to -51.0f
                    // Example: current_coolant_temp = (int)((message.data[1] * 0.75f) - 46.0f);
                    current_coolant_temp = (int)((message.data[1] * 0.75f) - 48.0f);
                }
                break;

            // --- FRAME: DME4 (Fuel Consumption & Voltage) ---
            // The ECU sends a "Delta" (amount injected since the last frame), not an absolute total counter.
            case 0x545:
                if (message.data_length_code >= 4) {
                    // --- ECU VOLTAGE READING (Byte 3) ---
                    can_voltage = (float)message.data[3] * 0.1f;

                    // --- FUEL CONSUMPTION ALGORITHM ---
                    // Problem: "Injector Micro-pulses" - Calculating L/h frame-by-frame causes severe jitter,
                    // especially at idle where fuel pulses are tiny and inconsistent.
                    // Solution: "Time Window Accumulation Algorithm".
                    // We accumulate the fuel usage over a 2-second time window to prevent L/h jitter.
                    uint16_t delta_raw = (message.data[2] << 8) | message.data[1];
                    unsigned long current_time = millis();

                    if (!first_fuel_packet) {
                        unsigned long delta_time_ms = current_time - prev_fuel_time;

                        if (delta_time_ms > 0) {
                            // Static variables to accumulate data over a 2-second window
                            static uint32_t fuel_accumulated_raw = 0;
                            static unsigned long time_accumulated_ms = 0;

                            fuel_accumulated_raw += delta_raw;
                            time_accumulated_ms += delta_time_ms;

                            // Przeliczanie wyników co 2000 ms (2 sekundy) dla maksymalnej stabilności
                            if (time_accumulated_ms >= 2000) {
                                float actual_ul = (float)fuel_accumulated_raw * 0.128f;
                                float liters_consumed = actual_ul / 1000000.0f;
                                float hours_passed = (float)time_accumulated_ms / 3600000.0f;
                                
                                // Stabilne spalanie godzinowe
                                current_lph = liters_consumed / hours_passed;

                                // Zapis do uśredniania L/100km tylko podczas jazdy (>5 km/h)
                                // Ponieważ zapisujemy co 2 sekundy, bufor w logic.h obejmuje teraz znacznie dłuższą trasę
                                if (current_speed > 5) {
                                    float l_100km = (current_lph / (float)current_speed) * 100.0f;
                                    fuelAvg.add(l_100km);
                                }

                                // Reset akumulatorów dla kolejnego okna czasowego
                                fuel_accumulated_raw = 0;
                                time_accumulated_ms = 0;
                            }
                        }
                    } else {
                        first_fuel_packet = false;
                    }
                    prev_fuel_time = current_time;
                }
                break;
        }
    }
}

/* --- MAIN SETUP --- */
void setup() {
    Serial.begin(115200);

    // --- 1. INICJALIZACJA CAN BUS (TWAI) ---
    // Konfiguracja sprzętowa - TRYB AKTYWNY
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
    g_config.rx_queue_len = 64; // Default is 5, which causes ~75% frame loss during LVGL rendering
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    // Instalacja i start sterownika
    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
        Serial.println("CAN Driver Installed Successfully.");
        if (twai_start() == ESP_OK) {
            Serial.println("CAN Driver Started.");
        } else {
            Serial.println("CRITICAL: Failed to START CAN Driver.");
        }
    } else {
        Serial.println("CRITICAL: Failed to INSTALL CAN Driver.");
    }
    // ----------------------------------------

    // ILL PIN (Night Mode Config)
    pinMode(ILL_PIN, INPUT);

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
    // 1. GUI Update
    lv_timer_handler();

    // 2. CAN Processing (Passive Sniffing)
    processCAN();

    // 3. Hardware Readings
    handle_voltage(); // Aktualizuje zmienną 'current_voltage' z pinu 34
    read_night_mode();

    // 4. SENSOR FUSION (Voltage)
    float display_voltage = current_voltage; // Default to 100% hardware ADC
    
    // Safety check: prevents using stale 0V CAN data when the Engine/CAN bus is OFF/sleeping.
    if (can_voltage > 8.0f) {
        // Weighted Average Logic:
        // We give 70% weight to the hardware ADC for its fast response time,
        // and 30% weight to the CAN ECU reading for baseline stability.
        display_voltage = (current_voltage * 0.7f) + (can_voltage * 0.3f);
    }

    // 5. Logic Updates
    // Przekazanie przetworzonych wyników do interfejsu LVGL
    update_engine_status(current_coolant_temp, fuelAvg.getAverage(), current_speed, current_lph, is_night_mode);
    update_gauge_logic(display_voltage, is_night_mode);

    delay(2); // Small yield
}