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

    FWD_ENTRY,
    FWD_WAIT_CLOSE,
    FWD_PROCESS,
    FWD_EXIT,

    REV_ENTRY,
    REV_WAIT_CLOSE,
    REV_EXIT,

    EMERGENCY_STATE

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

// ==================== DEBOUNCE TIMERS ====================
unsigned long lastButtonTime[MAX_CHANNELS] = {0};
unsigned long lastSensorTime[MAX_CHANNELS] = {0};

// ==================== EMERGENCY BUTTON FLAG ====================
volatile bool emergencyTriggered = false;

// ==================== PIN ARRAYS ====================
const uint8_t buttonPins[MAX_CHANNELS] = {CH1_PUSH_BTN, CH2_PUSH_BTN, CH3_PUSH_BTN};
const uint8_t sensorPins[MAX_CHANNELS] = {CH1_SENSOR, CH2_SENSOR, CH3_SENSOR};

// ==================== ISR ====================
void IRAM_ATTR button0_isr()
{
    int id = 0;
    xQueueSendFromISR(queueButtons, &id, NULL);
}
void IRAM_ATTR button1_isr()
{
    int id = 1;
    xQueueSendFromISR(queueButtons, &id, NULL);
}
void IRAM_ATTR button2_isr()
{
    int id = 2;
    xQueueSendFromISR(queueButtons, &id, NULL);
}

void IRAM_ATTR sensor0_isr()
{
    int id = 0;
    xQueueSendFromISR(queueSensors, &id, NULL);
}
void IRAM_ATTR sensor1_isr()
{
    int id = 1;
    xQueueSendFromISR(queueSensors, &id, NULL);
}
void IRAM_ATTR sensor2_isr()
{
    int id = 2;
    xQueueSendFromISR(queueSensors, &id, NULL);
}

void IRAM_ATTR emergency_isr()
{
    emergencyTriggered = true;
}

// ==================== RELAY HELPERS ====================
void lockDoor(int ch)
{
    if (ch == 1)
        digitalWrite(CH1_LOCK, RELAY_LOCK);
    if (ch == 2)
        digitalWrite(CH2_LOCK, RELAY_LOCK);
    if (ch == 3)
        digitalWrite(CH3_LOCK, RELAY_LOCK);
}

void unlockDoor(int ch)
{
    if (ch == 1)
        digitalWrite(CH1_LOCK, RELAY_UNLOCK);
    if (ch == 2)
        digitalWrite(CH2_LOCK, RELAY_UNLOCK);
    if (ch == 3)
        digitalWrite(CH3_LOCK, RELAY_UNLOCK);
}

void startSpray()
{
    digitalWrite(RELAY4_SPRAY_PIN, RELAY_SPRAY_ON);
}

void stopSpray()
{
    digitalWrite(RELAY4_SPRAY_PIN, RELAY_SPRAY_OFF);
}

bool isDoorClosed(int ch)
{
    if (ch == 1)
        return digitalRead(CH1_SENSOR) == SENSOR_CLOSED;
    if (ch == 2)
        return digitalRead(CH2_SENSOR) == SENSOR_CLOSED;
    if (ch == 3)
        return digitalRead(CH3_SENSOR) == SENSOR_CLOSED;
    return false;
}

bool isDoorLocked(int ch)
{
    if (ch == 1)
        return digitalRead(CH1_LOCK) == RELAY_LOCK;
    if (ch == 2)
        return digitalRead(CH2_LOCK) == RELAY_LOCK;
    if (ch == 3)
        return digitalRead(CH3_LOCK) == RELAY_LOCK;
    return false;
}

// ==================== AUXILIARY LIGHT (exact spec from project report) ====================
void updateAuxLight()
{
    bool allClosedAndLocked = isDoorClosed(1) && isDoorClosed(2) &&
                              isDoorLocked(1) && isDoorLocked(2);
    bool timedActive = (systemState == FWD_PROCESS);
    bool inEmergency = (systemState == EMERGENCY_STATE);

    if (allClosedAndLocked && !timedActive && !inEmergency)
        digitalWrite(AUX_LIGHT_PIN, LOW);
    else
        digitalWrite(AUX_LIGHT_PIN, HIGH);
}

// ==================== INDICATOR LEDs (steady state - matches project report) ====================
void updateIndicators()
{
    switch (systemState)
    {
    case INIT_CHECK:
    case IDLE:
        digitalWrite(CH1_GREEN_LED, HIGH);
        digitalWrite(CH2_GREEN_LED, HIGH);
        digitalWrite(CH1_RED_LED, LOW);
        break;

    case EMERGENCY_STATE:
        digitalWrite(CH1_GREEN_LED, LOW);
        digitalWrite(CH2_GREEN_LED, LOW);
        digitalWrite(CH1_RED_LED, HIGH);
        break;

    default: // active / transition / timed states
        digitalWrite(CH1_GREEN_LED, LOW);
        digitalWrite(CH2_GREEN_LED, LOW);
        digitalWrite(CH1_RED_LED, HIGH);
        break;
    }
}

// ==================== SAFETY CHECKS ====================
void checkEmergency()
{
    bool door1Unlocked = !isDoorLocked(1);
    bool door2Unlocked = !isDoorLocked(2);

    if (door1Unlocked && door2Unlocked && systemState != EMERGENCY_STATE)
    {
#if DEBUG_MODE
        Serial.println("[EMERGENCY] Both doors unlocked");
#endif
        systemState = EMERGENCY_STATE;
        stateStartTime = millis();
    }
}

void checkProcessSafety()
{
    if (systemState == FWD_PROCESS)
    {
        if (!isDoorClosed(1) || !isDoorClosed(2) ||
            !isDoorLocked(1) || !isDoorLocked(2))
        {
#if DEBUG_MODE
            Serial.println("[EMERGENCY] Door opened or unlocked during spray process");
#endif
            systemState = EMERGENCY_STATE;
            stateStartTime = millis();
        }
    }
}

// ==================== EMERGENCY HANDLER ====================
void handleEmergency()
{
    stopSpray();
    lockDoor(1);
    lockDoor(2);
}

// ==================== INIT CHECK ====================
void handleInitCheck()
{
    if (isDoorClosed(1) && isDoorClosed(2) &&
        isDoorLocked(1) && isDoorLocked(2))
    {
#if DEBUG_MODE
        Serial.println("[INFO] System safe - Entering IDLE");
#endif
        systemState = IDLE;
    }
}

// ==================== MODE A BUTTON ====================
void handleModeA_Button(int btnID)
{
    if (systemState != IDLE)
        return;

    switch (btnID)
    {
    case 0: // CH1 - Forward
#if DEBUG_MODE
        Serial.println("[INFO] Forward entry initiated - Channel 1");
#endif
        unlockDoor(1);
        lockDoor(2);
        systemState = FWD_ENTRY;
        stateStartTime = millis();
        break;

    case 1: // CH2 - Reverse
#if DEBUG_MODE
        Serial.println("[INFO] Reverse entry initiated - Channel 2");
#endif
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
    case FWD_ENTRY:
    case FWD_WAIT_CLOSE:
        if (sensorID == 0 && digitalRead(CH1_SENSOR) == SENSOR_CLOSED)
        {
#if DEBUG_MODE
            Serial.println("[INFO] Door 1 closed - Starting spray cycle");
#endif
            lockDoor(1);
            startSpray();
            systemState = FWD_PROCESS;
            stateStartTime = millis();
        }
        break;

    case REV_WAIT_CLOSE:
        if (sensorID == 1 && digitalRead(CH2_SENSOR) == SENSOR_CLOSED)
        {
#if DEBUG_MODE
            Serial.println("[INFO] Reverse path - Opening Door 1");
#endif
            unlockDoor(1);
            systemState = REV_EXIT;
        }
        break;

    case REV_EXIT:
        if (sensorID == 0 && digitalRead(CH1_SENSOR) == SENSOR_CLOSED)
        {
#if DEBUG_MODE
            Serial.println("[INFO] Reverse cycle complete - Returning to IDLE");
#endif
            lockDoor(1);
            lockDoor(2);
            systemState = IDLE;
        }
        break;

    case FWD_EXIT:
        if (sensorID == 1 && digitalRead(CH2_SENSOR) == SENSOR_CLOSED)
        {
#if DEBUG_MODE
            Serial.println("[INFO] Forward cycle complete - Returning to IDLE");
#endif
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
    checkEmergency();
    checkProcessSafety();

    switch (systemState)
    {
    case INIT_CHECK:
        handleInitCheck();
        break;

    case FWD_ENTRY:
        if (isTimeout(AUTO_RELOCK_TIMEOUT))
        {
#if DEBUG_MODE
            Serial.println("[INFO] Auto relock Door 1 after timeout");
#endif
            lockDoor(1);
            systemState = FWD_WAIT_CLOSE;
        }
        break;

    case FWD_PROCESS:
        if (isTimeout(SPRAY_DURATION))
        {
#if DEBUG_MODE
            Serial.println("[INFO] Spray cycle completed - Opening Door 2");
#endif
            stopSpray();
            unlockDoor(2);
            systemState = FWD_EXIT;
            // Buzzer pulse to be added here when buzzer is implemented
        }
        break;

    case REV_ENTRY:
        if (isTimeout(AUTO_RELOCK_TIMEOUT))
        {
#if DEBUG_MODE
            Serial.println("[INFO] Auto relock Door 2 after timeout");
#endif
            lockDoor(2);
            systemState = REV_WAIT_CLOSE;
        }
        break;

    case EMERGENCY_STATE:
        handleEmergency();
        if (isTimeout(EMERGENCY_TIMEOUT))
        {
#if DEBUG_MODE
            Serial.println("[INFO] Emergency timeout expired - Returning to IDLE");
#endif
            systemState = IDLE;
        }
        break;

    default:
        break;
    }
}

// ==================== TASKS ====================

void buttonTask(void *pvParameters)
{
    int btnID;
    while (true)
    {
        if (xQueueReceive(queueButtons, &btnID, portMAX_DELAY))
        {
            unsigned long now = millis();
            if (now - lastButtonTime[btnID] >= BTN_DEBOUNCE_TIME)
            {
                lastButtonTime[btnID] = now;
                if (current_mode == MODE_A)
                    handleModeA_Button(btnID);
            }
        }
    }
}

void sensorTask(void *pvParameters)
{
    int sensorID;
    while (true)
    {
        if (xQueueReceive(queueSensors, &sensorID, portMAX_DELAY))
        {
            unsigned long now = millis();
            if (now - lastSensorTime[sensorID] >= SENSOR_DEBOUNCE_TIME)
            {
                lastSensorTime[sensorID] = now;

                if (current_mode == MODE_A)
                    handleModeA_Sensor(sensorID);

#if DEBUG_DEBOUNCE_OPERATIONS
                Serial.print("[DEBUG] Sensor debounced - ID: ");
                Serial.println(sensorID);
#endif
            }
        }
    }
}

void controlTask(void *pvParameters)
{
    while (true)
    {
        if (current_mode == MODE_A)
        {
            if (emergencyTriggered)
            {
#if DEBUG_MODE
                Serial.println("[EMERGENCY] Emergency button pressed");
#endif
                systemState = EMERGENCY_STATE;
                stateStartTime = millis();
                emergencyTriggered = false;
            }

            handleModeA_Logic();
        }

        updateAuxLight();
        updateIndicators();

        vTaskDelay(pdMS_TO_TICKS(MAIN_LOOP_DELAY));
    }
}

// ==================== SETUP ====================
void setup()
{
    Serial.begin(SERIAL_BAUD);

    queueButtons = xQueueCreate(10, sizeof(int));
    queueSensors = xQueueCreate(10, sizeof(int));

    // Pin configuration
    pinMode(CH1_LOCK, OUTPUT);
    pinMode(CH2_LOCK, OUTPUT);
    pinMode(CH3_LOCK, OUTPUT);

    pinMode(RELAY4_SPRAY_PIN, OUTPUT);
    pinMode(AUX_LIGHT_PIN, OUTPUT);

    pinMode(CH1_RED_LED, OUTPUT);
    pinMode(CH1_GREEN_LED, OUTPUT);
    pinMode(CH2_RED_LED, OUTPUT);
    pinMode(CH2_GREEN_LED, OUTPUT);
    pinMode(CH3_RED_LED, OUTPUT);
    pinMode(CH3_GREEN_LED, OUTPUT);

    pinMode(CH1_PUSH_BTN, INPUT);
    pinMode(CH2_PUSH_BTN, INPUT);
    pinMode(CH1_SENSOR, INPUT);
    pinMode(CH2_SENSOR, INPUT);
    pinMode(EMERGENCY_BTN_PIN, INPUT);

    // Safe initial state
    lockDoor(1);
    lockDoor(2);
    lockDoor(3);
    stopSpray();

    digitalWrite(CH1_GREEN_LED, HIGH);
    digitalWrite(CH2_GREEN_LED, HIGH);
    digitalWrite(CH1_RED_LED, LOW);

    systemState = INIT_CHECK;

    // Interrupts
    attachInterrupt(digitalPinToInterrupt(CH1_PUSH_BTN), button0_isr, FALLING);
    attachInterrupt(digitalPinToInterrupt(CH2_PUSH_BTN), button1_isr, FALLING);

    attachInterrupt(digitalPinToInterrupt(CH1_SENSOR), sensor0_isr, FALLING);
    attachInterrupt(digitalPinToInterrupt(CH2_SENSOR), sensor1_isr, FALLING);

    attachInterrupt(digitalPinToInterrupt(EMERGENCY_BTN_PIN), emergency_isr, FALLING);

    // Tasks
    xTaskCreate(buttonTask, "ButtonTask", 2048, NULL, 10, NULL);
    xTaskCreate(sensorTask, "SensorTask", 2048, NULL, 10, NULL);
    xTaskCreate(controlTask, "ControlTask", 4096, NULL, 10, NULL);

#if DEBUG_MODE
    Serial.println("[INFO] A5 Decontamination Chamber - Firmware v1.1");
    Serial.println("[INFO] Mode A initialized successfully");
    Serial.println("[INFO] All safety interlocks active");
#endif
}

void loop()
{
    vTaskDelay(pdMS_TO_TICKS(1000));
}