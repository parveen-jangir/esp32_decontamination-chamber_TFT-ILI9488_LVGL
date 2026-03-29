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
    IDLE,
    FWD_ENTRY,
    FWD_WAIT_CLOSE,
    FWD_PROCESS,
    FWD_EXIT,

    REV_ENTRY,
    REV_WAIT_CLOSE,
    REV_EXIT

} SystemState;

volatile SystemState systemState = IDLE;

// ==================== TIMERS ====================
unsigned long stateStartTime = 0;

bool isTimeout(unsigned long duration)
{
    return millis() - stateStartTime >= duration;
}

// ==================== QUEUES ====================
QueueHandle_t queueButtons;
QueueHandle_t queueSensors;

// ==================== PIN ARRAYS ====================
const uint8_t buttonPins[MAX_CHANNELS] = {CH1_PUSH_BTN, CH2_PUSH_BTN, CH3_PUSH_BTN};
const uint8_t sensorPins[MAX_CHANNELS] = {CH1_SENSOR, CH2_SENSOR, CH3_SENSOR};

// ==================== ISR ====================
void IRAM_ATTR button0_isr() { int id = 0; xQueueSendFromISR(queueButtons, &id, NULL); }
void IRAM_ATTR button1_isr() { int id = 1; xQueueSendFromISR(queueButtons, &id, NULL); }
void IRAM_ATTR button2_isr() { int id = 2; xQueueSendFromISR(queueButtons, &id, NULL); }

void IRAM_ATTR sensor0_isr() { int id = 0; xQueueSendFromISR(queueSensors, &id, NULL); }
void IRAM_ATTR sensor1_isr() { int id = 1; xQueueSendFromISR(queueSensors, &id, NULL); }
void IRAM_ATTR sensor2_isr() { int id = 2; xQueueSendFromISR(queueSensors, &id, NULL); }

// ==================== RELAY HELPERS ====================
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

void startSpray()
{
    digitalWrite(RELAY4_SPRAY_PIN, RELAY_SPRAY_ON);
}

void stopSpray()
{
    digitalWrite(RELAY4_SPRAY_PIN, RELAY_SPRAY_OFF);
}

// ==================== MODE A BUTTON ====================
void handleModeA_Button(int btnID)
{
    if (systemState != IDLE) return;

    switch (btnID)
    {
    case 0: // CH1
        Serial.println("➡ Forward Entry");
        unlockDoor(1);
        lockDoor(2);
        systemState = FWD_ENTRY;
        stateStartTime = millis();
        break;

    case 1: // CH2
        Serial.println("⬅ Reverse Entry");
        unlockDoor(2);
        lockDoor(1);
        systemState = REV_ENTRY;
        stateStartTime = millis();
        break;
    }
}

// ==================== MODE A SENSOR ====================
void handleModeA_Sensor(int sensorID)
{
    switch (systemState)
    {
    case FWD_WAIT_CLOSE:
        if (sensorID == 0 && digitalRead(CH1_SENSOR) == SENSOR_CLOSED)
        {
            Serial.println("Door1 Closed → Start Spray");
            startSpray();
            systemState = FWD_PROCESS;
            stateStartTime = millis();
        }
        break;

    case REV_WAIT_CLOSE:
        if (sensorID == 1 && digitalRead(CH2_SENSOR) == SENSOR_CLOSED)
        {
            Serial.println("Reverse → Open Door1");
            unlockDoor(1);
            systemState = REV_EXIT;
        }
        break;

    case REV_EXIT:
        if (sensorID == 0 && digitalRead(CH1_SENSOR) == SENSOR_CLOSED)
        {
            Serial.println("Reverse Complete → IDLE");
            lockDoor(1);
            lockDoor(2);
            systemState = IDLE;
        }
        break;

    case FWD_EXIT:
        if (sensorID == 1 && digitalRead(CH2_SENSOR) == SENSOR_CLOSED)
        {
            Serial.println("Forward Complete → IDLE");
            lockDoor(1);
            lockDoor(2);
            systemState = IDLE;
        }
        break;
    }
}

// ==================== MODE A LOGIC ====================
void handleModeA_Logic()
{
    switch (systemState)
    {
    case FWD_ENTRY:
        if (isTimeout(AUTO_RELOCK_TIMEOUT))
        {
            Serial.println("Auto Relock Door1");
            lockDoor(1);
            systemState = FWD_WAIT_CLOSE;
        }
        break;

    case FWD_PROCESS:
        if (isTimeout(SPRAY_DURATION))
        {
            Serial.println("Spray Done → Open Door2");
            stopSpray();
            unlockDoor(2);
            systemState = FWD_EXIT;
        }
        break;

    case REV_ENTRY:
        if (isTimeout(AUTO_RELOCK_TIMEOUT))
        {
            Serial.println("Auto Relock Door2");
            lockDoor(2);
            systemState = REV_WAIT_CLOSE;
        }
        break;

    default:
        break;
    }
}

// ==================== TASKS ====================

// Button Task
void buttonTask(void *pvParameters)
{
    int btnID;
    while (true)
    {
        if (xQueueReceive(queueButtons, &btnID, portMAX_DELAY))
        {
            if (current_mode == MODE_A)
                handleModeA_Button(btnID);
        }
    }
}

// Sensor Task
void sensorTask(void *pvParameters)
{
    int sensorID;
    while (true)
    {
        if (xQueueReceive(queueSensors, &sensorID, portMAX_DELAY))
        {
            if (current_mode == MODE_A)
                handleModeA_Sensor(sensorID);
        }
    }
}

// Control Task
void controlTask(void *pvParameters)
{
    while (true)
    {
        if (current_mode == MODE_A)
            handleModeA_Logic();

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ==================== SETUP ====================
void setup()
{
    Serial.begin(SERIAL_BAUD);

    queueButtons = xQueueCreate(10, sizeof(int));
    queueSensors = xQueueCreate(10, sizeof(int));

    // Pin setup
    pinMode(CH1_LOCK, OUTPUT);
    pinMode(CH2_LOCK, OUTPUT);
    pinMode(RELAY4_SPRAY_PIN, OUTPUT);

    pinMode(CH1_PUSH_BTN, INPUT);
    pinMode(CH2_PUSH_BTN, INPUT);
    pinMode(CH1_SENSOR, INPUT);
    pinMode(CH2_SENSOR, INPUT);

    // Safe state
    lockDoor(1);
    lockDoor(2);
    stopSpray();

    // Interrupts
    attachInterrupt(digitalPinToInterrupt(CH1_PUSH_BTN), button0_isr, FALLING);
    attachInterrupt(digitalPinToInterrupt(CH2_PUSH_BTN), button1_isr, FALLING);

    attachInterrupt(digitalPinToInterrupt(CH1_SENSOR), sensor0_isr, FALLING);
    attachInterrupt(digitalPinToInterrupt(CH2_SENSOR), sensor1_isr, FALLING);

    // Tasks
    xTaskCreate(buttonTask, "ButtonTask", 2048, NULL, 10, NULL);
    xTaskCreate(sensorTask, "SensorTask", 2048, NULL, 10, NULL);
    xTaskCreate(controlTask, "ControlTask", 4096, NULL, 10, NULL);

    Serial.println("✅ Mode A System Ready");
}

void loop()
{
    vTaskDelay(pdMS_TO_TICKS(1000));
}