#include <lvgl.h>
#include <TFT_eSPI.h>
#include "ui.h"

TFT_eSPI tft = TFT_eSPI();

// Buffer
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[320 * 20];

/* ================= DISPLAY FLUSH ================= */
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)&color_p->full, w * h, true);
    tft.endWrite();

    lv_disp_flush_ready(disp);
}


void my_touch_read(lv_indev_drv_t * indev, lv_indev_data_t * data)
{
    uint16_t touchX, touchY;

    bool touched = tft.getTouch(&touchX, &touchY);

    if (!touched) {
        data->state = LV_INDEV_STATE_RELEASED;
    } else {
        data->state = LV_INDEV_STATE_PRESSED;

        data->point.x = touchX;
        data->point.y = touchY;
    }
}

/* ================= SETUP ================= */

void setup()
{
    Serial.begin(115200);

    tft.begin();
    tft.setRotation(0);   // Keep same rotation as calibration
    
    uint16_t calData[5] = { 113, 3608, 212, 3684, 4 };
    tft.setTouch(calData);

    lv_init();

    // Buffer init
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, 320 * 20);

    // Display driver
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);

    disp_drv.hor_res = 320;
    disp_drv.ver_res = 480;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;

    lv_disp_drv_register(&disp_drv);

    // 🔴 ADD TOUCH DRIVER (IMPORTANT)
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);

    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touch_read;

    lv_indev_drv_register(&indev_drv);

    ui_init();
}

/* ================= LOOP ================= */

void loop()
{
    lv_timer_handler();
    delay(5);
}