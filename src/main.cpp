#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "config.h"

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
unsigned long stateStartTime = 0;

// ==================== QUEUES ====================
QueueHandle_t queueButtons;
QueueHandle_t queueSensors;

// ==================== HELPERS ====================
bool isTimeout(unsigned long t)
{
    return millis() - stateStartTime >= t;
}

// Door and lock control functions
bool isDoorClosed(int ch)
{
    if (ch == 1) return digitalRead(CH1_SENSOR) == DOOR_CLOSED;
    if (ch == 2) return digitalRead(CH2_SENSOR) == DOOR_CLOSED;
    return false;
}

void lockDoor(int ch)
{
    if (ch == 1) digitalWrite(CH1_LOCK, RELAY_LOCK);
    if (ch == 2) digitalWrite(CH2_LOCK, RELAY_LOCK);
}

void unlockDoor(int ch)
{
    if (ch == 1) digitalWrite(CH1_LOCK, RELAY_UNLOCK);
    if (ch == 2) digitalWrite(CH2_LOCK, RELAY_UNLOCK);
}

void startSpray() { digitalWrite(RELAY4_SPRAY_PIN, RELAY_SPRAY_ON); }
void stopSpray()  { digitalWrite(RELAY4_SPRAY_PIN, RELAY_SPRAY_OFF); }

// ==================== LED + AUX ====================
void setGreen(int ch, bool state)
{
    if (ch == 1) digitalWrite(CH1_GREEN_LED, state);
    if (ch == 2) digitalWrite(CH2_GREEN_LED, state);
}

void setRed(bool state)
{
    digitalWrite(CH1_RED_LED, state); // shared
}

void setAux(bool state)
{
    digitalWrite(AUX_LIGHT_PIN, state);
}

// ==================== ISR ====================
void IRAM_ATTR button0_isr(){ int id=0; xQueueSendFromISR(queueButtons,&id,NULL); }
void IRAM_ATTR button1_isr(){ int id=1; xQueueSendFromISR(queueButtons,&id,NULL); }

void IRAM_ATTR sensor0_isr(){ int id=0; xQueueSendFromISR(queueSensors,&id,NULL); }
void IRAM_ATTR sensor1_isr(){ int id=1; xQueueSendFromISR(queueSensors,&id,NULL); }

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

    unlockDoor(entryChannel);
    lockDoor(exitChannel);

    setGreen(entryChannel, true);
    setAux(true);

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
                lockDoor(entryChannel);
                Serial.println(F("[INFO] Entry door closed - Starting process in 1 second"));
                vTaskDelay(pdMS_TO_TICKS(1000));
                setRed(true);
                setGreen(entryChannel, false);
                startSpray();

                stateStartTime = millis();
                systemState = PROCESS_RUNNING;
            }
        }

        else if (entryChannel == 2 && sensorID == 1 && isDoorClosed(entryChannel))
        {
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
        if (isDoorClosed(exitChannel))
        {
            lockDoor(exitChannel);
            setRed(false);
            setAux(false);
            Serial.println(F("[INFO] Exit door closed - Process completed"));
            systemState = INIT_CHECK;
        }
    }
}

// ==================== WARNING ====================
void handleWarning()
{
    static bool blink=false;
    static unsigned long t=0;

    if (millis()-t>500)
    {
        t=millis();
        blink=!blink;
        setRed(blink);
    }

    setAux(true);

    if (isDoorClosed(1) && isDoorClosed(2))
    {
        setRed(false);
        Serial.println(F("[INFO] Warning resolved - System reset to IDLE"));
        systemState = IDLE;
    }
}

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
    }

    if (isTimeout(EMERGENCY_TIMEOUT))
    {
        if (isDoorClosed(1) && isDoorClosed(2))
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

// ==================== SYSTEM FAILURE ====================
void handleSystemFailure()
{
    static bool blink=false;
    static unsigned long t=0;

    if (millis()-t>200)
    {
        t=millis();
        blink=!blink;
        setRed(blink);
        setGreen(1, blink);
        setGreen(2, blink);
        setAux(true);
    }
}

void preFailure(){
    stopSpray();
    setAux(true);
    lockDoor(1);
    lockDoor(2);
}

// ==================== CONTROL LOGIC ====================
void controlTask(void *pv)
{
    while (true)
    {
        // Check states of buttons and sensors
        bool ch1_sensor = digitalRead(CH1_SENSOR); // true if OPEN
        bool ch2_sensor = digitalRead(CH2_SENSOR); // true if OPEN
        bool ch1_button = digitalRead(CH1_PUSH_BTN); // true if NOT pressed (active LOW)
        bool ch2_button = digitalRead(CH2_PUSH_BTN); // true if NOT pressed (active LOW)
        bool emergency_btn = digitalRead(EMERGENCY_BTN_PIN); // true if NOT pressed (active LOW)

        // Emergency button
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
            stateStartTime = millis();
            systemState = EMERGENCY_STATE;
        }

        // If door opened during processing
        if (systemState == PROCESS_RUNNING)
        {
            if ((ch1_sensor == DOOR_OPEN || ch2_sensor == DOOR_OPEN) && systemState != SYSTEM_FAILURE)
            {
                Serial.println(F("[ERROR] Door opened during process - Emergency state"));
                preFailure();
                stateStartTime = millis();
                systemState = SYSTEM_FAILURE;
            }
        }

        // Both doors should not be open at the same time
        if ((ch1_sensor == DOOR_OPEN && ch2_sensor == DOOR_OPEN) && systemState != SYSTEM_FAILURE)
        {
            Serial.println(F("[ERROR] Both doors are open - Emergency state"));
            preFailure();
            stateStartTime = millis();
            systemState = SYSTEM_FAILURE;
        }

        if ((ch1_sensor == DOOR_OPEN || ch2_sensor == DOOR_OPEN) && systemState != SYSTEM_FAILURE)
        {
            if(systemState == IDLE)
            {
                setAux(true);
                Serial.println(F("[WARNING] Door opened while idle - Warning state"));
                preFailure();
                stateStartTime = millis();
                systemState = SYSTEM_FAILURE;
            }
        }

        switch (systemState)
        {
        case INIT_CHECK:
            if (ch1_sensor == DOOR_CLOSED && ch2_sensor == DOOR_CLOSED)
            {
                setRed(false);
                setGreen(1,false);
                setGreen(2,false);
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
            handleGreenLED(500, entryChannel);
            break;

        case PROCESS_RUNNING:
            if (isTimeout(SPRAY_DURATION))
            {
                stopSpray();
                unlockDoor(exitChannel);
        
                stateStartTime = millis();
                Serial.println(F("[INFO] Spray duration completed"));
                systemState = EXIT_OPEN;
            }
            break;

        case EXIT_OPEN:
            if (isTimeout(AUTO_RELOCK_TIMEOUT))
            {
                if (isDoorClosed(exitChannel))
                {
                    lockDoor(exitChannel);
                    Serial.println(F("[INFO] Exit door auto-locked due to timeout"));
                }
                setAux(false);
                setRed(false);
                Serial.println(F("[INFO] Process completed 1"));
                systemState = INIT_CHECK;
            }

            if (!isDoorClosed(exitChannel))
            {
                stateStartTime = millis();
                Serial.println(F("[INFO] Exit door opened - Waiting for close"));
                systemState = WAIT_EXIT_CLOSE;
            }
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
            if (current_mode == MODE_A || current_mode == MODE_B)
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
            if (current_mode == MODE_A || current_mode == MODE_B)
                handleModeAB_Sensor(id);
        }
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
    pinMode(RELAY4_SPRAY_PIN,OUTPUT);
    pinMode(AUX_LIGHT_PIN,OUTPUT);

    pinMode(CH1_PUSH_BTN,INPUT);
    pinMode(CH2_PUSH_BTN,INPUT);
    pinMode(CH1_SENSOR,INPUT);
    pinMode(CH2_SENSOR,INPUT);
    pinMode(EMERGENCY_BTN_PIN,INPUT_PULLUP);

    pinMode(CH1_GREEN_LED,OUTPUT);
    pinMode(CH2_GREEN_LED,OUTPUT);
    pinMode(CH1_RED_LED,OUTPUT);

    attachInterrupt(digitalPinToInterrupt(CH1_PUSH_BTN),button0_isr,FALLING);
    attachInterrupt(digitalPinToInterrupt(CH2_PUSH_BTN),button1_isr,FALLING);

    attachInterrupt(digitalPinToInterrupt(CH1_SENSOR),sensor0_isr,FALLING);
    attachInterrupt(digitalPinToInterrupt(CH2_SENSOR),sensor1_isr,FALLING);

    xTaskCreate(buttonTask,"btn",2048,NULL,10,NULL);
    xTaskCreate(sensorTask,"sen",2048,NULL,10,NULL);
    xTaskCreate(controlTask,"ctrl",4096,NULL,10,NULL);

    Serial.println("✅ SYSTEM READY (MODE A)");
}

void loop()
{
    vTaskDelay(pdMS_TO_TICKS(1000));
}