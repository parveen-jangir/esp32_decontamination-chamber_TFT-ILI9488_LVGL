#include <lvgl.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

// Buffer
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[480 * 20];

// Label pointer (global for update)
lv_obj_t * slider_label;

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

/* ================= EVENTS ================= */

// Button event
static void btn_event_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        Serial.println("Button Pressed!");
    }
}

// Slider event
static void slider_event_cb(lv_event_t * e)
{
    lv_obj_t * slider = lv_event_get_target(e);
    int value = lv_slider_get_value(slider);

    char buf[20];
    sprintf(buf, "Value: %d", value);
    lv_label_set_text(slider_label, buf);
}

/* ================= UI ================= */

void ui_init()
{
    // Title
    lv_obj_t * title = lv_label_create(lv_scr_act());
    lv_label_set_text(title, "LVGL Demo UI");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // Button
    lv_obj_t * btn = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn, 120, 50);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, -40);
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Press Me");
    lv_obj_center(btn_label);

    // Slider
    lv_obj_t * slider = lv_slider_create(lv_scr_act());
    lv_obj_set_width(slider, 300);
    lv_obj_align(slider, LV_ALIGN_CENTER, 0, 40);
    lv_slider_set_range(slider, 0, 100);
    lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Slider value label
    slider_label = lv_label_create(lv_scr_act());
    lv_label_set_text(slider_label, "Value: 0");
    lv_obj_align(slider_label, LV_ALIGN_CENTER, 0, 80);
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
    tft.setRotation(1);   // Keep same rotation as calibration

    // 🔴 ADD THIS RIGHT HERE
    uint16_t calData[5] = { 249, 3661, 86, 3625, 7 };
    tft.setTouch(calData);

    lv_init();

    // Buffer init
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, 480 * 20);

    // Display driver
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);

    disp_drv.hor_res = 480;
    disp_drv.ver_res = 320;
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