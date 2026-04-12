SET(SOURCES ui_comp_changescreen.c
    ui_comp_modselbtn.c
    ui_comp.c
    ui_Screen1.c
    ui_Screen3.c
    ui_theme_manager.c
    ui_themes.c
    ui.c
    ui_comp_hook.c
    ui_helpers.c
    ui_img_image_2_png.c
    ui_img_arrow_3_png.c
    ui_img_image_8_png.c
    fonts/ui_font_Montserrat150.c
    fonts/ui_font_Montserrat60.c
    fonts/ui_font_Montserrat90.c)

add_library(ui ${SOURCES})
