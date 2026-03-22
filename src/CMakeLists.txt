SET(SOURCES screens/ui_Screen1.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    images/ui_img_gauge_bg_png.c
    images/ui_img_car_batter_png.c
    images/ui_img_temp_png.c
    images/ui_img_gas_png.c
    images/ui_img_needle_blue_png.c
    images/ui_img_needle_green_png.c
    images/ui_img_needle_red_png.c
    images/ui_img_hub_b_png.c
    images/ui_img_hub_g_png.c
    images/ui_img_hub_r_png.c
    fonts/ui_font_Chakra20Bold.c
    fonts/ui_font_ChakraPetchBold10.c
    fonts/ui_font_ChakraPetchBold18.c
    fonts/ui_font_ChakraPetchBold22.c)

add_library(ui ${SOURCES})
