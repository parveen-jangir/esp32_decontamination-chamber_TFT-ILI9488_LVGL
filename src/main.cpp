#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "config.h"
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

// ==================== Manual & Speed Door Modes ====================
volatile bool isSpeedDoor = false;

// ==================== MODE ====================
volatile int current_mode = MODE_B;

// ==================== STATE ====================
typedef enum
{
    INIT_CHECK,
    IDLE,

    ENTRY_OPEN,
    WAIT_ENTRY_CLOSE,
    PROCESS_RUNNING,
    EXIT_OPEN,
    WAIT_EXIT_CLOSE,

    WARNING_TIMEOUT,
    EMERGENCY_STATE,
    SYSTEM_FAILURE

} SystemState;

volatile SystemState systemState = INIT_CHECK;

// ==================== CONTEXT ====================
uint8_t entryChannel = 0; // 1 or 2
uint8_t exitChannel = 0;  // 1 or 2 (opposite of entryChannel)
uint8_t additionalChannel = 3; // For MODE C (channel 3)
unsigned long stateStartTime = 0;

// ==================== QUEUES ====================
QueueHandle_t queueButtons;
QueueHandle_t queueSensors;

// ==================== DEBOUNCE VARIABLES ====================
// Button debounce state
volatile unsigned long ch1_button_last_time = 0;
volatile unsigned long ch2_button_last_time = 0;
volatile unsigned long ch3_button_last_time = 0;
volatile bool ch1_button_pressed = false;
volatile bool ch2_button_pressed = false;
volatile bool ch3_button_pressed = false;

// Sensor debounce state
volatile unsigned long ch1_sensor_last_time = 0;
volatile unsigned long ch2_sensor_last_time = 0;
volatile unsigned long ch3_sensor_last_time = 0;
volatile bool ch1_sensor_closed = false;
volatile bool ch2_sensor_closed = false;
volatile bool ch3_sensor_closed = false;

// ==================== HELPERS ====================
bool isTimeout(unsigned long t)
{
    return millis() - stateStartTime >= t;
}

// Door and lock control functions
bool isDoorClosed(int ch)
{
    if (ch == 1) return ch1_sensor_closed;  // Use debounced state
    if (ch == 2) return ch2_sensor_closed;  // Use debounced state
    if (ch == 3) return ch3_sensor_closed; //  Use debounced state
    return false;
}

void lockDoor(int ch)
{
    if (ch == 1) digitalWrite(CH1_LOCK, RELAY_LOCK);
    if (ch == 2) digitalWrite(CH2_LOCK, RELAY_LOCK);
    if (ch == 3) digitalWrite(CH3_LOCK, RELAY_LOCK);
}

void unlockDoor(int ch)
{
    if (ch == 1) digitalWrite(CH1_LOCK, RELAY_UNLOCK);
    if (ch == 2) digitalWrite(CH2_LOCK, RELAY_UNLOCK);
    if (ch == 3) digitalWrite(CH3_LOCK, RELAY_UNLOCK);
}

void startSpray() { digitalWrite(RELAY4_SPRAY_PIN, RELAY_SPRAY_ON); }
void stopSpray()  { digitalWrite(RELAY4_SPRAY_PIN, RELAY_SPRAY_OFF); }

// ==================== LED + AUX ====================
void setGreen(int ch, bool state)
{
    if (ch == 1) digitalWrite(CH1_GREEN_LED, state);
    if (ch == 2) digitalWrite(CH2_GREEN_LED, state);
    if (ch == 3) digitalWrite(CH3_GREEN_LED, state);
}

void setRed(bool state)
{
    digitalWrite(CH1_RED_LED, state); // shared
}

void setAux(bool state)
{
    digitalWrite(AUX_LIGHT_PIN, state);
}

// ==================== DEBOUNCE TASK ====================
void debounceTask(void *pv)
{
    while (true)
    {
        unsigned long current_time = millis();
        
        // ----- BUTTON DEBOUNCE -----
        // CH1 Button
        bool ch1_raw = digitalRead(CH1_PUSH_BTN);
        if (ch1_raw == BUTTON_PRESSED && !ch1_button_pressed)
        {
            if (current_time - ch1_button_last_time >= BTN_DEBOUNCE_TIME)
            {
                ch1_button_pressed = true;
                ch1_button_last_time = current_time;
                int id = 0;
                xQueueSend(queueButtons, &id, 0);
                #if DEBUG_DEBOUNCE_OPERATIONS
                Serial.println(F("[DEBOUNCE] CH1 Button PRESSED (debounced)"));
                #endif
            }
        }
        else if (ch1_raw == BUTTON_RELEASED && ch1_button_pressed)
        {
            if (current_time - ch1_button_last_time >= BTN_DEBOUNCE_TIME)
            {
                ch1_button_pressed = false;
                ch1_button_last_time = current_time;
                #if DEBUG_DEBOUNCE_OPERATIONS
                Serial.println(F("[DEBOUNCE] CH1 Button RELEASED (debounced)"));
                #endif
            }
        }
        
        // CH2 Button
        bool ch2_raw = digitalRead(CH2_PUSH_BTN);
        if (ch2_raw == BUTTON_PRESSED && !ch2_button_pressed)
        {
            if (current_time - ch2_button_last_time >= BTN_DEBOUNCE_TIME)
            {
                ch2_button_pressed = true;
                ch2_button_last_time = current_time;
                int id = 1;
                xQueueSend(queueButtons, &id, 0);
                #if DEBUG_DEBOUNCE_OPERATIONS
                Serial.println(F("[DEBOUNCE] CH2 Button PRESSED (debounced)"));
                #endif
            }
        }
        else if (ch2_raw == BUTTON_RELEASED && ch2_button_pressed)
        {
            if (current_time - ch2_button_last_time >= BTN_DEBOUNCE_TIME)
            {
                ch2_button_pressed = false;
                ch2_button_last_time = current_time;
                #if DEBUG_DEBOUNCE_OPERATIONS
                Serial.println(F("[DEBOUNCE] CH2 Button RELEASED (debounced)"));
                #endif
            }
        }

        // CH3 Button
        bool ch3_raw = digitalRead(CH3_PUSH_BTN);
        if (ch3_raw == BUTTON_PRESSED && !ch3_button_pressed)
        {
            if (current_time - ch3_button_last_time >= BTN_DEBOUNCE_TIME)
            {
                ch3_button_pressed = true;
                ch3_button_last_time = current_time;
                int id = 2;
                xQueueSend(queueButtons, &id, 0);
                #if DEBUG_DEBOUNCE_OPERATIONS
                Serial.println(F("[DEBOUNCE] CH3 Button PRESSED (debounced)"));
                #endif
            }
        }
        else if (ch3_raw == BUTTON_RELEASED && ch3_button_pressed)
        {
            if (current_time - ch3_button_last_time >= BTN_DEBOUNCE_TIME)
            {
                ch3_button_pressed = false;
                ch3_button_last_time = current_time;
                #if DEBUG_DEBOUNCE_OPERATIONS
                Serial.println(F("[DEBOUNCE] CH3 Button RELEASED (debounced)"));
                #endif
            }
        }
        
        // ----- SENSOR DEBOUNCE -----
        // CH1 Sensor
        bool ch1_sensor_raw = digitalRead(CH1_SENSOR);
        if (ch1_sensor_raw == DOOR_CLOSED && !ch1_sensor_closed)
        {
            if (current_time - ch1_sensor_last_time >= SENSOR_DEBOUNCE_TIME)
            {
                ch1_sensor_closed = true;
                ch1_sensor_last_time = current_time;
                int id = 0;
                xQueueSend(queueSensors, &id, 0);
                #if DEBUG_DEBOUNCE_OPERATIONS
                Serial.println(F("[DEBOUNCE] CH1 Sensor CLOSED (debounced)"));
                #endif
            }
        }
        else if (ch1_sensor_raw == DOOR_OPEN && ch1_sensor_closed)
        {
            if (current_time - ch1_sensor_last_time >= SENSOR_DEBOUNCE_TIME)
            {
                ch1_sensor_closed = false;
                ch1_sensor_last_time = current_time;
                #if DEBUG_DEBOUNCE_OPERATIONS
                Serial.println(F("[DEBOUNCE] CH1 Sensor OPENED (debounced)"));
                #endif
            }
        }
        
        // CH2 Sensor
        bool ch2_sensor_raw = digitalRead(CH2_SENSOR);
        if (ch2_sensor_raw == DOOR_CLOSED && !ch2_sensor_closed)
        {
            if (current_time - ch2_sensor_last_time >= SENSOR_DEBOUNCE_TIME)
            {
                ch2_sensor_closed = true;
                ch2_sensor_last_time = current_time;
                int id = 1;
                xQueueSend(queueSensors, &id, 0);
                #if DEBUG_DEBOUNCE_OPERATIONS
                Serial.println(F("[DEBOUNCE] CH2 Sensor CLOSED (debounced)"));
                #endif
            }
        }
        else if (ch2_sensor_raw == DOOR_OPEN && ch2_sensor_closed)
        {
            if (current_time - ch2_sensor_last_time >= SENSOR_DEBOUNCE_TIME)
            {
                ch2_sensor_closed = false;
                ch2_sensor_last_time = current_time;
                #if DEBUG_DEBOUNCE_OPERATIONS
                Serial.println(F("[DEBOUNCE] CH2 Sensor OPENED (debounced)"));
                #endif
            }
        }

        // CH3 Sensor
        bool ch3_sensor_raw = digitalRead(CH3_SENSOR);
        if (ch3_sensor_raw == DOOR_CLOSED && !ch3_sensor_closed)
        {
            if (current_time - ch3_sensor_last_time >= SENSOR_DEBOUNCE_TIME)
            {
                ch3_sensor_closed = true;
                ch3_sensor_last_time = current_time;
                int id = 2;
                xQueueSend(queueSensors, &id, 0);
                #if DEBUG_DEBOUNCE_OPERATIONS
                Serial.println(F("[DEBOUNCE] CH3 Sensor CLOSED (debounced)"));
                #endif
            }
        }
        else if (ch3_sensor_raw == DOOR_OPEN && ch3_sensor_closed)
        {
            if (current_time - ch3_sensor_last_time >= SENSOR_DEBOUNCE_TIME)
            {
                ch3_sensor_closed = false;
                ch3_sensor_last_time = current_time;
                #if DEBUG_DEBOUNCE_OPERATIONS
                Serial.println(F("[DEBOUNCE] CH3 Sensor OPENED (debounced)"));
                #endif
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(5)); // 5ms poll interval
    }
}

// ==================== BUTTON HANDLER ====================
void handleModeAB_Button(int btnID)
{
    if (systemState != IDLE) return;

    if (btnID == 0) // CH1 → Forward
    {
        entryChannel = 1;
        exitChannel = 2;
        Serial.println(F("[INFO] CH1 button pressed - Starting process"));
    }
    else if (btnID == 1) // CH2 → Reverse
    {
        entryChannel = 2;
        exitChannel = 1;
        Serial.println(F("[INFO] CH2 button pressed - Starting process"));
    }
    else if (btnID == 2 && current_mode == MODE_C) // CH3 → Additional channel for MODE C
    {
        entryChannel = 3;
        exitChannel = 1; // In MODE C, exit is always channel 1
        Serial.println(F("[INFO] CH3 button pressed - Starting process (MODE C)"));
    }
    else
    {
        Serial.println(F("[ERROR] Invalid button ID or button not allowed in current mode"));
        return;
    }

    unlockDoor(entryChannel);
    lockDoor(exitChannel);

    setGreen(entryChannel, true);
    setGreen(exitChannel, false);
    if(current_mode == MODE_C) setGreen(additionalChannel, false);
    setAux(true);
    setRed(true);

    stateStartTime = millis();
    systemState = ENTRY_OPEN;
}

// ==================== SENSOR HANDLER ====================
void handleModeAB_Sensor(int sensorID)
{
    if (systemState == WAIT_ENTRY_CLOSE)
    {
        if ((entryChannel == 1 && sensorID == 0) || (entryChannel == 2 && sensorID == 1 && current_mode == MODE_B))
        {
            if (isDoorClosed(entryChannel))
            {
                stateStartTime = millis();
                lockDoor(entryChannel);
                Serial.println(F("[INFO] Entry door closed - Starting process in 1 second"));
                vTaskDelay(pdMS_TO_TICKS(1000));
                setRed(true);
                setGreen(exitChannel, false);
                setGreen(entryChannel, false);
                if(current_mode == MODE_C) setGreen(additionalChannel, false);
                startSpray();

                systemState = PROCESS_RUNNING;
            }
        }

        else if (entryChannel == 2 && sensorID == 1 && isDoorClosed(entryChannel))
        {
            stateStartTime = millis();
            lockDoor(entryChannel);
            setRed(true);
            setGreen(entryChannel, false);
            Serial.println(F("[INFO] Reverse entry door closed - open exit door in 1 second"));
            vTaskDelay(pdMS_TO_TICKS(1000));
            unlockDoor(exitChannel);
            systemState = EXIT_OPEN;
        }
        else if (entryChannel == 3 && sensorID == 2 && current_mode == MODE_C && isDoorClosed(entryChannel))
        {
            stateStartTime = millis();
            lockDoor(entryChannel);
            setRed(true);
            setGreen(entryChannel, false);
            Serial.println(F("[INFO] Reverse entry door closed - open exit door in 1 second"));
            vTaskDelay(pdMS_TO_TICKS(1000));
            unlockDoor(exitChannel);
            systemState = EXIT_OPEN;
        }
    }

    if (systemState == WAIT_EXIT_CLOSE)
    {
        if (isDoorClosed(exitChannel) && current_mode != MODE_C)
        {
            lockDoor(exitChannel);
            setRed(false);
            setAux(false);
            Serial.println(F("[INFO] Exit door closed - Process completed"));
            systemState = INIT_CHECK;
        }
        else if (isDoorClosed(exitChannel) && isDoorClosed(additionalChannel) && current_mode == MODE_C)
        {
            lockDoor(exitChannel);
            lockDoor(additionalChannel);
            setRed(false);
            setAux(false);
            Serial.println(F("[INFO] Exit doors closed - Process completed (MODE C)"));
            systemState = INIT_CHECK;
        }
    }
}

// ==================== WARNING ====================
// void handleWarning()
// {
//     static bool blink=false;
//     static unsigned long t=0;
//     if (millis()-t>500)
//     {
//         t=millis();
//         blink=!blink;
//         setRed(blink);
//     }
//     setAux(true);
//     if (isDoorClosed(1) && isDoorClosed(2))
//     {
//         setRed(false);
//         Serial.println(F("[INFO] Warning resolved - System reset to IDLE"));
//         systemState = IDLE;
//     }
// }

// ==================== EMERGENCY ====================
void handleEmergency()
{
    static bool blink=false;
    static unsigned long t=0;

    if (millis()-t>300)
    {
        t=millis();
        blink=!blink;
        setRed(blink);
        setGreen(1, blink);
        setGreen(2, blink);
        if(current_mode == MODE_C) setGreen(3, blink);
    }

    if (isTimeout(EMERGENCY_TIMEOUT))
    {
        if (isDoorClosed(1) && isDoorClosed(2) && (current_mode != MODE_C || isDoorClosed(3)))
        {
            setRed(false);
            setGreen(1,false);
            setGreen(2,false);
            setAux(false);
            Serial.println(F("[INFO] Emergency resolved - System reset to IDLE"));
            systemState = INIT_CHECK;
        }
    }
}

// ==================== GREEN LED BLINK ====================
void handleGreenLED(unsigned long time, int ch = 1)
{
    static bool blink=false;
    static unsigned long t=0;

    if (millis()-t>time)
    {
        t=millis();
        blink=!blink;
        setGreen(ch, blink);
    }
}

// ==================== RED LED BLINK =====================
void handleRedLED(unsigned long time)
{
    static bool blink=false;
    static unsigned long t=0;

    if (millis()-t>time)
    {
        t=millis();
        blink=!blink;
        setRed(blink);
    }
}

// ==================== SYSTEM FAILURE ====================
void handleSystemFailure()
{
    static bool blink=false;
    static unsigned long t=0;

    if (millis()-t>200)
    {
        t=millis();
        blink=!blink;
        setRed(!blink);
        setGreen(1, blink);
        setGreen(2, blink);
        if (current_mode == MODE_C) setGreen(3, blink);
        setAux(true);
    }
}

void preFailure(){
    stopSpray();
    setAux(true);
    lockDoor(1);
    lockDoor(2);
    if (current_mode == MODE_C) lockDoor(3);
}

// ==================== CONTROL LOGIC ====================
void controlTask(void *pv)
{
    while (true)
    {
        // Use debounced states instead of raw reads
        bool ch1_sensor = !ch1_sensor_closed; // true if OPEN
        bool ch2_sensor = !ch2_sensor_closed; // true if OPEN
        bool ch3_sensor = !ch3_sensor_closed; // true if OPEN
        bool ch1_button = !ch1_button_pressed; // true if NOT pressed (active LOW)
        bool ch2_button = !ch2_button_pressed; // true if NOT pressed (active LOW)
        bool ch3_button = !ch3_button_pressed; // true if NOT pressed (active LOW)
        bool emergency_btn = digitalRead(EMERGENCY_BTN_PIN); // true if NOT pressed (active LOW)

        // Emergency button (no debounce - direct for safety)
        if (emergency_btn == BUTTON_PRESSED)
        {
            Serial.println(F("[EMERGENCY] Emergency button pressed"));
            stopSpray();
            unlockDoor(1);
            unlockDoor(2);
            setAux(true);
            setRed(true);
            setGreen(1,true);
            setGreen(2,true);
            if(current_mode == MODE_C)
            {
                setGreen(3,true);
                unlockDoor(3);
            }
            stateStartTime = millis();
            systemState = EMERGENCY_STATE;
        }

        // Both doors should not be open at the same time
        if (current_mode != MODE_C)
        {
            if ((ch1_sensor == DOOR_OPEN && ch2_sensor == DOOR_OPEN) && systemState != SYSTEM_FAILURE && systemState != EMERGENCY_STATE)
            {
                Serial.println(F("[ERROR] Both doors are open - Emergency state"));
                preFailure();
                stateStartTime = millis();
                systemState = SYSTEM_FAILURE;
            }
        }
        else
        {
            if (((ch1_sensor == DOOR_OPEN + ch2_sensor == DOOR_OPEN + ch3_sensor == DOOR_OPEN) >=2) && systemState != SYSTEM_FAILURE && systemState != EMERGENCY_STATE)
            {
                Serial.println(F("[ERROR] All doors are open - Emergency state"));
                preFailure();
                stateStartTime = millis();
                systemState = SYSTEM_FAILURE;
            }
        }

        switch (systemState)
        {
        case IDLE:
            if (ch1_sensor == DOOR_OPEN || ch2_sensor == DOOR_OPEN || (current_mode == MODE_C && ch3_sensor == DOOR_OPEN))
            {
                setAux(true);
                Serial.println(F("[WARNING] Door opened while idle - Warning state"));
                preFailure();
                stateStartTime = millis();
                systemState = SYSTEM_FAILURE;
            }
            break;

        case INIT_CHECK:
            if (current_mode != MODE_C)
            {
                if (ch1_sensor == DOOR_CLOSED && ch2_sensor == DOOR_CLOSED)
                {
                    setRed(false);
                    setGreen(1, true);
                    setGreen(2, true);
                    lockDoor(1);
                    lockDoor(2);
                    stopSpray();
                    setAux(false);
                    Serial.println(F("[INFO] System initialized - IDLE state"));
                    systemState = IDLE;
                }
                else
                {
                    setAux(true);
                    Serial.println(F("[ERROR] System initialization failed - Please close both doors"));
                    preFailure();
                    stateStartTime = millis();
                    systemState = SYSTEM_FAILURE;
                }
            }
            else
            {
                if (ch1_sensor == DOOR_CLOSED && ch2_sensor == DOOR_CLOSED && ch3_sensor == DOOR_CLOSED)
                {
                    setRed(false);
                    setGreen(1, true);
                    setGreen(2, true);
                    setGreen(3, true);
                    lockDoor(1);
                    lockDoor(2);
                    lockDoor(3);
                    stopSpray();
                    setAux(false);
                    Serial.println(F("[INFO] System initialized - IDLE state (MODE C)"));
                    systemState = IDLE;
                }
                else
                {
                    setAux(true);
                    Serial.println(F("[ERROR] System initialization failed - Please close all doors (MODE C)"));
                    preFailure();
                    stateStartTime = millis();
                    systemState = SYSTEM_FAILURE;
                }
            }
            break;

        case ENTRY_OPEN:
            handleGreenLED(500, entryChannel);

            // Timeout completed still door is closed
            if (isTimeout(AUTO_RELOCK_TIMEOUT) && isDoorClosed(entryChannel))
            {
                lockDoor(entryChannel);
                setGreen(entryChannel,false);
                Serial.println(F("[INFO] Entry door auto-locked due to timeout"));
                systemState = INIT_CHECK;
            }

            // Move to next state if door opens before timeout
            if (!isDoorClosed(entryChannel))
            {
                stateStartTime = millis();
                Serial.println(F("[INFO] Entry door opened - Waiting for close"));
                // lockDoor(entryChannel); this will required here in speed door
                systemState = WAIT_ENTRY_CLOSE;
            }
            break;

        case WAIT_ENTRY_CLOSE:
            if (isSpeedDoor && isTimeout(SPEED_DOOR_TIMEOUT))
            {
                lockDoor(entryChannel);
            }

            handleGreenLED(500, entryChannel);
            break;
        
        case WAIT_EXIT_CLOSE:
            if (isSpeedDoor && isTimeout(SPEED_DOOR_TIMEOUT))
            {
                lockDoor(exitChannel);  // Fixed: was entryChannel
                if (current_mode == MODE_C && exitChannel != 1) lockDoor(additionalChannel); // For MODE C
            }
            handleRedLED(500);
            break;

        case PROCESS_RUNNING:

            // If door opened during processing
            if ((ch1_sensor == DOOR_OPEN || ch2_sensor == DOOR_OPEN || (current_mode == MODE_C && ch3_sensor == DOOR_OPEN)))
            {
                Serial.println(F("[ERROR] Door opened during process - Emergency state"));
                preFailure();
                stateStartTime = millis();
                systemState = SYSTEM_FAILURE;
            }

            if (isTimeout(SPRAY_DURATION))
            {
                stateStartTime = millis();
                stopSpray();
                vTaskDelay(pdMS_TO_TICKS(500)); // Short delay to ensure spray is fully stopped before locking
                unlockDoor(exitChannel);
                if (current_mode == MODE_C) unlockDoor(additionalChannel); // For MODE C, unlock additional channel
        
                Serial.println(F("[INFO] Spray duration completed"));
                systemState = EXIT_OPEN;
            }
            handleRedLED(500);
            break;

        case EXIT_OPEN:
            if (isTimeout(AUTO_RELOCK_TIMEOUT))
            {
                if (isDoorClosed(exitChannel) && (current_mode != MODE_C || isDoorClosed(additionalChannel)))
                {
                    lockDoor(exitChannel);
                    if (current_mode == MODE_C)
                        lockDoor(additionalChannel);
                    Serial.println(F("[INFO] Exit door auto-locked due to timeout"));
                    setAux(false);
                    setRed(false);
                    Serial.println(F("[INFO] Process completed 1"));
                    systemState = INIT_CHECK;
                }
            }

            if (current_mode != MODE_C)
            {
                if (!isDoorClosed(exitChannel))
                {
                    stateStartTime = millis();
                    Serial.println(F("[INFO] Exit door opened - Waiting for close"));
                    systemState = WAIT_EXIT_CLOSE;
                }
            }
            else
            {
                if (!isDoorClosed(exitChannel) || !isDoorClosed(additionalChannel))
                {
                    stateStartTime = millis();
                    Serial.println(F("[INFO] Exit door opened - Waiting for close (MODE C)"));
                    systemState = WAIT_EXIT_CLOSE;
                }
            }
            handleRedLED(500);
            break;

        case WARNING_TIMEOUT:
            // handleWarning();
            break;

        case EMERGENCY_STATE:
            handleEmergency();
            break;

        case SYSTEM_FAILURE:
            handleSystemFailure();
            if (millis() - stateStartTime > SYSTEM_FAILURE_TIMEOUT)
            {
                stateStartTime = millis();
                Serial.println(F("[INFO] System failure timeout completed - Resetting system"));
                systemState = INIT_CHECK;
            }
            break;

        default:
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ==================== TASKS ====================
void buttonTask(void *pv)
{
    int id;
    while (true)
    {
        if (xQueueReceive(queueButtons,&id,portMAX_DELAY))
        {
            handleModeAB_Button(id);
        }
    }
}

void sensorTask(void *pv)
{
    int id;
    while (true)
    {
        if (xQueueReceive(queueSensors,&id,portMAX_DELAY))
        {
            handleModeAB_Sensor(id);
        }
    }
}

void handleDisplayTask(void *pv)
{
    while (true)
    {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
// ==================== SETUP ====================
void setup()
{
    Serial.begin(SERIAL_BAUD);

    queueButtons = xQueueCreate(10,sizeof(int));
    queueSensors = xQueueCreate(10,sizeof(int));

    pinMode(CH1_LOCK,OUTPUT);
    pinMode(CH2_LOCK,OUTPUT);
    pinMode(CH3_LOCK,OUTPUT);
    pinMode(RELAY4_SPRAY_PIN,OUTPUT);
    pinMode(AUX_LIGHT_PIN,OUTPUT);

    // Enable internal pull-ups for active-LOW inputs
    pinMode(CH1_PUSH_BTN, INPUT_PULLUP);
    pinMode(CH2_PUSH_BTN, INPUT_PULLUP);
    pinMode(CH3_PUSH_BTN, INPUT_PULLUP);
    pinMode(CH1_SENSOR, INPUT_PULLUP);
    pinMode(CH2_SENSOR, INPUT_PULLUP);
    pinMode(CH3_SENSOR, INPUT_PULLUP);
    pinMode(EMERGENCY_BTN_PIN, INPUT_PULLUP);

    pinMode(CH1_GREEN_LED,OUTPUT);
    pinMode(CH2_GREEN_LED,OUTPUT);
    pinMode(CH3_GREEN_LED,OUTPUT);
    pinMode(CH1_RED_LED,OUTPUT);

    xTaskCreate(debounceTask, "debounce", 2048, NULL, 15, NULL);  // High priority for debounce
    xTaskCreate(buttonTask,"btn",2048,NULL,10,NULL);
    xTaskCreate(sensorTask,"sen",2048,NULL,10,NULL);
    xTaskCreate(controlTask,"ctrl",4096,NULL,10,NULL);
    xTaskCreate(handleDisplayTask,"disp",4096,NULL,10,NULL);

    Serial.println("[INFO] SYSTEM READY (MODE A), Door type: " + String(isSpeedDoor ? "SPEED" : "MANUAL"));
    Serial.println("[INFO] Software debouncing enabled: BTN=" + String(BTN_DEBOUNCE_TIME) + "ms, SENSOR=" + String(SENSOR_DEBOUNCE_TIME) + "ms");

    tft.begin();
    tft.setRotation(1);   // Keep same rotation as calibration

    // ADD THIS RIGHT HERE
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

    // ADD TOUCH DRIVER (IMPORTANT)
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);

    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touch_read;

    lv_indev_drv_register(&indev_drv);

    ui_init();
}

void loop()
{
    if(Serial.available())
    {
        char cmd = Serial.read();
        if (cmd == 'a' || cmd == 'A')
        {
            current_mode = MODE_A;
            Serial.println("[Serial] Switched to MODE A");
        }
        else if (cmd == 'b' || cmd == 'B')
        {
            current_mode = MODE_B;
            Serial.println("[Serial] Switched to MODE B");
        }
        else if (cmd == 'c' || cmd == 'C')
        {
            current_mode = MODE_C;
            Serial.println("[Serial] Switched to MODE C");
        }
        else if (cmd == 's' || cmd == 'S')
        {
            isSpeedDoor = !isSpeedDoor;
            Serial.print("[Serial] Speed Door Mode: ");
            Serial.println(isSpeedDoor ? "ON" : "OFF");
        }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
}