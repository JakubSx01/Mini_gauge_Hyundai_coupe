#include <Arduino.h>
#include <lvgl.h>
#include "ui.h"

// --- CONFIGURATION ---
#define MIN_VAL 8.0f
#define MAX_VAL 18.0f
#define START_ANGLE 135
#define END_ANGLE 405

static void hide_all_needles() {
    if (ui_ImgNeedleBlue)
        lv_obj_add_flag(ui_ImgNeedleBlue, LV_OBJ_FLAG_HIDDEN);
    if (ui_ImgNeedleGreen)
        lv_obj_add_flag(ui_ImgNeedleGreen, LV_OBJ_FLAG_HIDDEN);
    if (ui_ImgNeedleRed)
        lv_obj_add_flag(ui_ImgNeedleRed, LV_OBJ_FLAG_HIDDEN);

    if (ui_ImgHubBlue)
        lv_obj_add_flag(ui_ImgHubBlue, LV_OBJ_FLAG_HIDDEN);
    if (ui_ImgHubGreen)
        lv_obj_add_flag(ui_ImgHubGreen, LV_OBJ_FLAG_HIDDEN);
    if (ui_ImgHubRed)
        lv_obj_add_flag(ui_ImgHubRed, LV_OBJ_FLAG_HIDDEN);
}

void update_gauge_logic(float v, bool value_valid, bool night_mode) {
    lv_color_t color_text_active =
        night_mode ? lv_color_hex(0x999999) : lv_color_white();
    lv_color_t color_text_inactive =
        night_mode ? lv_color_hex(0x333333) : lv_color_hex(0x555555);

    lv_color_t c_blue =
        night_mode ? lv_color_hex(0x004466) : lv_color_hex(0x00CCFF);
    lv_color_t c_green =
        night_mode ? lv_color_hex(0x006622) : lv_color_hex(0x00FF44);
    lv_color_t c_red =
        night_mode ? lv_color_hex(0x660000) : lv_color_hex(0xFF1F1F);
    lv_color_t c_default =
        night_mode ? lv_color_hex(0x444444) : lv_color_hex(0x888888);

    lv_obj_t *labels[] = {
        ui_uiLbl8, ui_uiLbl10, ui_uiLbl12,
        ui_uiLbl14, ui_uiLbl16, ui_uiLbl18
    };
    int values[] = {8, 10, 12, 14, 16, 18};

    hide_all_needles();

    if (!value_valid) {
        if (ui_uiArcMain) {
            lv_arc_set_value(ui_uiArcMain, 0);
            lv_obj_set_style_arc_color(
                ui_uiArcMain, c_default, LV_PART_INDICATOR);
            lv_obj_set_style_shadow_opa(
                ui_uiArcMain, 0, LV_PART_INDICATOR);
        }

        for (int i = 0; i < 6; i++) {
            if (labels[i]) {
                lv_obj_set_style_text_color(
                    labels[i], color_text_inactive, 0);
            }
        }

        if (ui_uiBatteryImg) {
            lv_obj_set_style_img_recolor(ui_uiBatteryImg, c_default, 0);
            lv_obj_set_style_img_recolor_opa(
                ui_uiBatteryImg, LV_OPA_COVER, 0);
        }

        if (ui_LblValue) {
            lv_label_set_text(ui_LblValue, "--.-");
            lv_obj_set_style_text_color(ui_LblValue, c_default, 0);
        }
        return;
    }

    // Clamp only the graphical scale. The numeric label keeps the real value.
    float gauge_v = v;
    if (gauge_v < MIN_VAL) gauge_v = MIN_VAL;
    if (gauge_v > MAX_VAL) gauge_v = MAX_VAL;

    float pct = (gauge_v - MIN_VAL) / (MAX_VAL - MIN_VAL);
    int16_t angle_x10 =
        (int16_t)((START_ANGLE + (pct * 270.0f)) * 10.0f);

    lv_color_t active_color;
    lv_obj_t *active_needle = NULL;

    if (v < 11.8f) {
        if (ui_ImgNeedleBlue)
            lv_obj_clear_flag(ui_ImgNeedleBlue, LV_OBJ_FLAG_HIDDEN);
        if (ui_ImgHubBlue)
            lv_obj_clear_flag(ui_ImgHubBlue, LV_OBJ_FLAG_HIDDEN);
        active_needle = ui_ImgNeedleBlue;
        active_color = c_blue;
    } else if (v > 14.8f) {
        if (ui_ImgNeedleRed)
            lv_obj_clear_flag(ui_ImgNeedleRed, LV_OBJ_FLAG_HIDDEN);
        if (ui_ImgHubRed)
            lv_obj_clear_flag(ui_ImgHubRed, LV_OBJ_FLAG_HIDDEN);
        active_needle = ui_ImgNeedleRed;
        active_color = c_red;
    } else {
        if (ui_ImgNeedleGreen)
            lv_obj_clear_flag(ui_ImgNeedleGreen, LV_OBJ_FLAG_HIDDEN);
        if (ui_ImgHubGreen)
            lv_obj_clear_flag(ui_ImgHubGreen, LV_OBJ_FLAG_HIDDEN);
        active_needle = ui_ImgNeedleGreen;
        active_color = c_green;
    }

    if (active_needle) {
        lv_img_set_angle(active_needle, angle_x10);
        if (night_mode) {
            lv_obj_set_style_img_recolor(
                active_needle, lv_color_black(), 0);
            lv_obj_set_style_img_recolor_opa(
                active_needle, LV_OPA_30, 0);
        } else {
            lv_obj_set_style_img_recolor_opa(
                active_needle, LV_OPA_TRANSP, 0);
        }
    }

    if (ui_uiArcMain) {
        lv_arc_set_value(ui_uiArcMain, (int)(pct * 100.0f));
        lv_obj_set_style_arc_color(
            ui_uiArcMain, active_color, LV_PART_INDICATOR);

        if (night_mode) {
            lv_obj_set_style_shadow_opa(
                ui_uiArcMain, 0, LV_PART_INDICATOR);
        } else {
            lv_obj_set_style_shadow_opa(
                ui_uiArcMain, 255, LV_PART_INDICATOR);
            lv_obj_set_style_shadow_color(
                ui_uiArcMain, active_color, LV_PART_INDICATOR);
        }
    }

    for (int i = 0; i < 6; i++) {
        if (labels[i]) {
            lv_color_t color =
                gauge_v >= ((float)values[i] - 0.2f)
                    ? color_text_active : color_text_inactive;
            lv_obj_set_style_text_color(labels[i], color, 0);
        }
    }

    if (ui_uiBatteryImg) {
        lv_obj_set_style_img_recolor(ui_uiBatteryImg, active_color, 0);
        lv_obj_set_style_img_recolor_opa(
            ui_uiBatteryImg, LV_OPA_COVER, 0);
    }

    if (ui_LblValue) {
        int voltage_tenths = (int)((v * 10.0f) + 0.5f);
        lv_label_set_text_fmt(ui_LblValue, "%d.%d",
                              voltage_tenths / 10,
                              voltage_tenths % 10);
        lv_obj_set_style_text_color(ui_LblValue, active_color, 0);
    }
}

// --- ENGINE STATUS LOGIC ---
void update_engine_status(int temp, bool temp_valid,
                          float avg_fuel, bool fuel_valid,
                          bool is_night_mode) {
    lv_color_t c_blue =
        is_night_mode ? lv_color_hex(0x004466) : lv_color_hex(0x0000FF);
    lv_color_t c_green =
        is_night_mode ? lv_color_hex(0x006622) : lv_color_hex(0x00FF00);
    lv_color_t c_orange =
        is_night_mode ? lv_color_hex(0x996600) : lv_color_hex(0xFFA500);
    lv_color_t c_red =
        is_night_mode ? lv_color_hex(0x660000) : lv_color_hex(0xFF0000);
    lv_color_t c_default =
        is_night_mode ? lv_color_hex(0x666666) : lv_color_hex(0xFFFFFF);

    lv_color_t temp_color = c_default;

    if (temp_valid) {
        if (temp < 40) temp_color = c_blue;
        else if (temp < 80) temp_color = c_green;
        else if (temp <= 100) temp_color = c_orange;
        else temp_color = c_red;
    }

    if (ui_uiTemplevel) {
        if (temp_valid) {
            lv_label_set_text_fmt(ui_uiTemplevel, "%d", temp);
        } else {
            lv_label_set_text(ui_uiTemplevel, "--");
        }
        lv_obj_set_style_text_color(ui_uiTemplevel, temp_color, 0);
    }

    if (ui_uiTempimg) {
        lv_obj_set_style_img_recolor(ui_uiTempimg, temp_color, 0);
        lv_obj_set_style_img_recolor_opa(
            ui_uiTempimg, LV_OPA_COVER, 0);
    }

    lv_color_t fuel_color = c_default;

    if (fuel_valid) {
        if (avg_fuel <= 7.0f) fuel_color = c_green;
        else if (avg_fuel <= 11.0f) fuel_color = c_orange;
        else fuel_color = c_red;
    }

    if (ui_uiGaslevel) {
        if (fuel_valid) {
            int fuel_tenths =
                (int)((avg_fuel * 10.0f) + 0.5f);
            lv_label_set_text_fmt(ui_uiGaslevel, "%d.%d",
                                  fuel_tenths / 10,
                                  fuel_tenths % 10);
        } else {
            lv_label_set_text(ui_uiGaslevel, "--.-");
        }
        lv_obj_set_style_text_color(ui_uiGaslevel, fuel_color, 0);
    }

    if (ui_uiGas) {
        lv_obj_set_style_img_recolor(ui_uiGas, fuel_color, 0);
        lv_obj_set_style_img_recolor_opa(
            ui_uiGas, LV_OPA_COVER, 0);
    }
}
