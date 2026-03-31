#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "config.h"

// ==================== MODE ====================
volatile int current_mode = DEFAULT_MODE;

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
void handleModeA_Button(int btnID)
{
    if (systemState != IDLE) return;

    if (btnID == 0) // CH1 → Forward
    {
        entryChannel = 1;
        exitChannel = 2;
    }
    else if (btnID == 1) // CH2 → Reverse
    {
        entryChannel = 2;
        exitChannel = 1;
    }

    unlockDoor(entryChannel);
    lockDoor(exitChannel);

    setGreen(entryChannel, true);
    setAux(true);

    systemState = ENTRY_OPEN;
    stateStartTime = millis();
}

// ==================== SENSOR HANDLER ====================
void handleModeA_Sensor(int sensorID)
{
    if (systemState == WAIT_ENTRY_CLOSE)
    {
        if (entryChannel == 1 && sensorID == 0 && isDoorClosed(entryChannel))
        {
            lockDoor(entryChannel);
            Serial.println("[INFO] Forward entry - Starting process in 1 second");
            vTaskDelay(pdMS_TO_TICKS(1000));
            startSpray();
            setRed(true);
            setGreen(1, false);

            stateStartTime = millis();
            systemState = PROCESS_RUNNING;
        }

        if (entryChannel == 2 && sensorID == 1 && isDoorClosed(entryChannel))
        {
            lockDoor(entryChannel);
            Serial.println("[INFO] Reverse entry - Starting process in 1 second");
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
        systemState = IDLE;
    }
}

// ==================== EMERGENCY ====================
void handleEmergency()
{
    stopSpray();
    lockDoor(1);
    lockDoor(2);

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

    setAux(true);

    if (isTimeout(EMERGENCY_TIMEOUT))
    {
        if (isDoorClosed(1) && isDoorClosed(2))
        {
            setRed(false);
            setGreen(1,false);
            setGreen(2,false);
            setAux(false);
            systemState = IDLE;
        }
    }
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
            systemState = EMERGENCY_STATE;
            stateStartTime = millis();
        }

        // If door opened during processing
        if (systemState == PROCESS_RUNNING)
        {
            if (ch1_sensor == DOOR_OPEN || ch2_sensor == DOOR_OPEN)
                systemState = SYSTEM_FAILURE;
        }

        // Both doors should not be open at the same time
        if (ch1_sensor == DOOR_OPEN && ch2_sensor == DOOR_OPEN)
        {
            systemState = SYSTEM_FAILURE;
        }

        if (ch1_sensor == DOOR_OPEN || ch2_sensor == DOOR_OPEN)
        {
            if(systemState == IDLE)
            {
                setAux(true);
                systemState = SYSTEM_FAILURE;
            }
        }

        switch (systemState)
        {
        case INIT_CHECK:
            if (ch1_sensor == DOOR_CLOSED && ch2_sensor == DOOR_CLOSED)
            {
                lockDoor(1);
                lockDoor(2);
                stopSpray();
                setAux(false);
                systemState = IDLE;
            }
            else
            {
                setAux(true);
                systemState = SYSTEM_FAILURE;
            }
            break;

        case ENTRY_OPEN:

            // Timeout completed still door is closed
            if (isTimeout(AUTO_RELOCK_TIMEOUT) && isDoorClosed(entryChannel))
            {
                lockDoor(entryChannel);
                setGreen(entryChannel,false);
                systemState = INIT_CHECK;
            }

            // Move to next state if door opens before timeout
            if (!isDoorClosed(entryChannel))
            {
                setGreen(entryChannel,false);
                stateStartTime = millis();
                vTaskDelay(pdMS_TO_TICKS(1000)); // Short delay to allow for door state
                lockDoor(entryChannel);
                systemState = WAIT_ENTRY_CLOSE;
            }
            break;

        case PROCESS_RUNNING:
            if (isTimeout(SPRAY_DURATION))
            {
                stopSpray();
                unlockDoor(exitChannel);
        
                stateStartTime = millis();
                systemState = EXIT_OPEN;
            }
            break;

        case EXIT_OPEN:
            if (isTimeout(AUTO_RELOCK_TIMEOUT))
            {
                if (isDoorClosed(exitChannel))
                {
                    lockDoor(exitChannel);
                }
                setAux(false);
                systemState = INIT_CHECK;
            }

            if (!isDoorClosed(exitChannel))
            {
                stateStartTime = millis();
                systemState = WAIT_EXIT_CLOSE;
            }
            break;

        case WARNING_TIMEOUT:
            handleWarning();
            break;

        case EMERGENCY_STATE:
            handleEmergency();
            break;

        case SYSTEM_FAILURE:
            setRed(true);

        default:
            break;
        }
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
            if (current_mode == MODE_A)
                handleModeA_Button(id);
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
            if (current_mode == MODE_A)
                handleModeA_Sensor(id);
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
    pinMode(EMERGENCY_BTN_PIN,INPUT);

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