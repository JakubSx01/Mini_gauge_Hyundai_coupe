#include <Arduino.h>
#include <lvgl.h>
#include "ui.h"

// --- CONFIGURATION ---
#define MIN_VAL 8.0f
#define MAX_VAL 18.0f
#define START_ANGLE 135
#define END_ANGLE 405

void update_gauge_logic(float v, bool night_mode) {

    // 1. Range Protection
    if (v < MIN_VAL) v = MIN_VAL;
    if (v > MAX_VAL) v = MAX_VAL;

    // 2. Calculations
    float range = MAX_VAL - MIN_VAL;
    float pct = (v - MIN_VAL) / range; // 0.0 do 1.0

    // Needle angle (x10 for precision)
    int16_t angle_x10 = (int16_t)((START_ANGLE + (pct * 270.0f)) * 10);

    // 1. COLOR DEFINITIONS (Day / Night Palette)
    lv_color_t color_text_active   = night_mode ? lv_color_hex(0x999999) : lv_color_white();
    lv_color_t color_text_inactive = night_mode ? lv_color_hex(0x333333) : lv_color_hex(0x555555);

    // Zone colors (Dimmed at night)
    lv_color_t c_blue   = night_mode ? lv_color_hex(0x004466) : lv_color_hex(0x00CCFF);
    lv_color_t c_green  = night_mode ? lv_color_hex(0x006622) : lv_color_hex(0x00FF44);
    lv_color_t c_red    = night_mode ? lv_color_hex(0x660000) : lv_color_hex(0xFF1F1F);


    lv_color_t active_color;
    lv_obj_t * active_needle = NULL;

    // 3. COLOR AND LAYER LOGIC
    // Hide all graphic variants
    
    if(ui_ImgNeedleBlue) lv_obj_add_flag(ui_ImgNeedleBlue, LV_OBJ_FLAG_HIDDEN);
    if(ui_ImgNeedleGreen) lv_obj_add_flag(ui_ImgNeedleGreen, LV_OBJ_FLAG_HIDDEN);
    if(ui_ImgNeedleRed) lv_obj_add_flag(ui_ImgNeedleRed, LV_OBJ_FLAG_HIDDEN);

    if(ui_ImgHubBlue) lv_obj_add_flag(ui_ImgHubBlue, LV_OBJ_FLAG_HIDDEN);
    if(ui_ImgHubGreen) lv_obj_add_flag(ui_ImgHubGreen, LV_OBJ_FLAG_HIDDEN);
    if(ui_ImgHubRed) lv_obj_add_flag(ui_ImgHubRed, LV_OBJ_FLAG_HIDDEN);

    // Select set based on voltage
    if (v < 11.8f) { // BLUE ZONE
        if(ui_ImgNeedleBlue) lv_obj_clear_flag(ui_ImgNeedleBlue, LV_OBJ_FLAG_HIDDEN);
        if(ui_ImgHubBlue) lv_obj_clear_flag(ui_ImgHubBlue, LV_OBJ_FLAG_HIDDEN);
        
        active_needle = ui_ImgNeedleBlue;
        active_color = c_blue;
    }
    else if (v > 14.8f) { // RED ZONE
        if(ui_ImgNeedleRed) lv_obj_clear_flag(ui_ImgNeedleRed, LV_OBJ_FLAG_HIDDEN);
        if(ui_ImgHubRed) lv_obj_clear_flag(ui_ImgHubRed, LV_OBJ_FLAG_HIDDEN);
        
        active_needle = ui_ImgNeedleRed;
        active_color = c_red;
    }
    else { // GREEN ZONE
        if(ui_ImgNeedleGreen) lv_obj_clear_flag(ui_ImgNeedleGreen, LV_OBJ_FLAG_HIDDEN);
        if(ui_ImgHubGreen) lv_obj_clear_flag(ui_ImgHubGreen, LV_OBJ_FLAG_HIDDEN);
        
        active_needle = ui_ImgNeedleGreen;
        active_color = c_green;
    }

    // 4. NEEDLE ROTATION
    // Dim the needle in night mode (recolor)
    if(active_needle) {
        lv_img_set_angle(active_needle, angle_x10);
        if(night_mode) {
             lv_obj_set_style_img_recolor(active_needle, lv_color_black(), 0);
             lv_obj_set_style_img_recolor_opa(active_needle, LV_OPA_30, 0); // 30% dimming
        } else {
             lv_obj_set_style_img_recolor_opa(active_needle, LV_OPA_TRANSP, 0);
        }
    }

    // 5. NATIVE ARC HANDLER (ui_uiArcMain)
    if (ui_uiArcMain) {
        // Fill percentage (0-100)
        lv_arc_set_value(ui_uiArcMain, (int)(pct * 100));
        
        // Change arc color (Indicator)
        lv_obj_set_style_arc_color(ui_uiArcMain, active_color, LV_PART_INDICATOR);
        
        // Change shadow color (Glow/Neon) - Disabled at night
        if(night_mode) {
             lv_obj_set_style_shadow_opa(ui_uiArcMain, 0, LV_PART_INDICATOR);
        } else {
             lv_obj_set_style_shadow_opa(ui_uiArcMain, 255, LV_PART_INDICATOR);
             lv_obj_set_style_shadow_color(ui_uiArcMain, active_color, LV_PART_INDICATOR);
        }
    }

    // 6. DIGIT HIGHLIGHTING
    lv_obj_t* labels[] = {ui_uiLbl8, ui_uiLbl10, ui_uiLbl12, ui_uiLbl14, ui_uiLbl16, ui_uiLbl18};
    int values[] = {8, 10, 12, 14, 16, 18};

    for(int i=0; i<6; i++) {
        if (v >= (float)values[i] - 0.2f) {
            lv_obj_set_style_text_color(labels[i], color_text_active, 0);
        } else {
            lv_obj_set_style_text_color(labels[i], color_text_inactive, 0);
        }
    }

    // 7. BATTERY ICON (ui_uiBatteryImg)
    if (ui_uiBatteryImg) {
        lv_obj_set_style_img_recolor(ui_uiBatteryImg, active_color, 0);
        lv_obj_set_style_img_recolor_opa(ui_uiBatteryImg, LV_OPA_COVER, 0);
    }

    // 8. CENTER TEXT (ui_LblValue)
    int v_int = (int)v;
    int v_dec = (int)((v - v_int) * 10);
    if(ui_LblValue) {
        lv_label_set_text_fmt(ui_LblValue, "%d.%d", v_int, v_dec);
        lv_obj_set_style_text_color(ui_LblValue, active_color, 0);
    }
}

// --- ENGINE STATUS LOGIC ---
void update_engine_status(int temp, float avg_fuel, int speed, float lph, bool is_night_mode) {
    
    // Color Palette (Night vs Day)
    lv_color_t c_blue   = is_night_mode ? lv_color_hex(0x004466) : lv_color_hex(0x0000FF);
    lv_color_t c_green  = is_night_mode ? lv_color_hex(0x006622) : lv_color_hex(0x00FF00);
    lv_color_t c_orange = is_night_mode ? lv_color_hex(0x996600) : lv_color_hex(0xFFA500);
    lv_color_t c_red    = is_night_mode ? lv_color_hex(0x660000) : lv_color_hex(0xFF0000);
    lv_color_t c_def    = is_night_mode ? lv_color_hex(0x666666) : lv_color_hex(0xFFFFFF);

    // 1. COOLANT TEMP LOGIC
    // < 40: Blue
    // 40-79: Green
    // 80-100: Orange
    // > 100: Red
    
    lv_color_t tempColor = c_def;

    if (temp < 40)       tempColor = c_blue;
    else if (temp < 80)  tempColor = c_green;
    else if (temp <= 100) tempColor = c_orange;
    else                 tempColor = c_red;

    // Update Temp UI
    if (ui_uiTemplevel) {
        lv_label_set_text_fmt(ui_uiTemplevel, "%d", temp);
        lv_obj_set_style_text_color(ui_uiTemplevel, tempColor, 0);
    }
    if (ui_uiTempimg) {
        lv_obj_set_style_img_recolor(ui_uiTempimg, tempColor, 0);
        lv_obj_set_style_img_recolor_opa(ui_uiTempimg, LV_OPA_COVER, 0);
    }

    // 2. AVG FUEL LOGIC
    // 0 - 7.0: Green (eco driving)
    // 7.0 - 11.0: Orange (normal driving)
    // > 11.0: Red (aggressive driving)

    lv_color_t fuelColor = c_def;

    if (avg_fuel <= 7.0f)        fuelColor = c_green;
    else if (avg_fuel <= 11.0f)  fuelColor = c_orange;
    else                         fuelColor = c_red;

    // Update Fuel UI
    if (ui_uiGaslevel) {
        // --- DYNAMIC FUEL DISPLAY ---
        if (speed > 5) {
            // Car is moving - show average consumption per 100km
            int fuel_int = (int)avg_fuel;
            int fuel_dec = (int)((avg_fuel - fuel_int) * 10);
            lv_label_set_text_fmt(ui_uiGaslevel, "%d.%d", fuel_int, fuel_dec);
        } else {
            // Car is stopped (or in traffic) - show instantaneous consumption per hour
            int lph_int = (int)lph;
            int lph_dec = (int)((lph - lph_int) * 10);
            lv_label_set_text_fmt(ui_uiGaslevel, "%d.%d", lph_int, lph_dec);
        }
        lv_obj_set_style_text_color(ui_uiGaslevel, fuelColor, 0);
    }
    if (ui_uiGas) {
        lv_obj_set_style_img_recolor(ui_uiGas, fuelColor, 0);
        lv_obj_set_style_img_recolor_opa(ui_uiGas, LV_OPA_COVER, 0);
    }
}
