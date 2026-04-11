SET(SOURCES ui_Screen1.c
    ui_Screen2.c
    ui.c
    ui_comp_hook.c
    ui_helpers.c
    fonts/ui_font_Montserrat150.c
    fonts/ui_font_Montserrat60.c
    fonts/ui_font_Montserrat90.c)

add_library(ui ${SOURCES})
