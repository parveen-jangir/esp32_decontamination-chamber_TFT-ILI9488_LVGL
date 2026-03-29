#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "config.h"

// ==================== GLOBAL VARIABLES ====================
const uint8_t buttonPins[MAX_CHANNELS] = {CH1_PUSH_BTN, CH2_PUSH_BTN, CH3_PUSH_BTN};
const uint8_t sensorPins[MAX_CHANNELS] = {CH1_SENSOR, CH2_SENSOR, CH3_SENSOR};
bool processing = false; // state of system (true = processing, false = idle)

// =============== PUSH BUTTONS ===============

QueueHandle_t queueGroup1;

#define CREATE_ISR_GROUP1(btn)                      \
  void IRAM_ATTR button##btn##_isr()                \
  {                                                 \
    static unsigned long lastTime = 0;              \
    unsigned long now = millis();                   \
    if (now - lastTime > 200)                       \
    {                                               \
      int btnID = btn;                              \
      xQueueSendFromISR(queueGroup1, &btnID, NULL); \
      lastTime = now;                               \
    }                                               \
  }

CREATE_ISR_GROUP1(0)
CREATE_ISR_GROUP1(1)
CREATE_ISR_GROUP1(2)

// =============== SENSOR BUTTONS ===============

QueueHandle_t queueGroup2;

#define CREATE_ISR_GROUP2(btn)                      \
  void IRAM_ATTR button##btn##_isr()                \
  {                                                 \
    static unsigned long lastTime = 0;              \
    unsigned long now = millis();                   \
    if (now - lastTime > 200)                       \
    {                                               \
      int btnID = btn;                              \
      xQueueSendFromISR(queueGroup2, &btnID, NULL); \
      lastTime = now;                               \
    }                                               \
  }

CREATE_ISR_GROUP2(3)
CREATE_ISR_GROUP2(4)
CREATE_ISR_GROUP2(5)

// ==================== TASK 1 - Handles only Group 1 ====================
void buttonTaskGroup1(void *pvParameters)
{
  int btnID;
  while (true)
  {
    if (xQueueReceive(queueGroup1, &btnID, portMAX_DELAY))
    {
      Serial.printf("🚀 [Group 1] Button %d PRESSED!\n", btnID);

      // ← Put your Group 1 actions here
      switch (btnID)
      {
      case 0:
        Serial.println("   → Action for Group1 Button 0");
        break;
      case 1:
        Serial.println("   → Action for Group1 Button 1");
        break;
      case 2:
        Serial.println("   → Action for Group1 Button 2");
        break;
      }
    }
  }
}

// ==================== TASK 2 - Handles only Group 2 ====================
void buttonTaskGroup2(void *pvParameters)
{
  int btnID;
  while (true)
  {
    if (xQueueReceive(queueGroup2, &btnID, portMAX_DELAY))
    {
      Serial.printf("🚀 [Group 2] Button %d PRESSED!\n", btnID);

      // ← Put your Group 2 actions here (completely different from Group 1)
      switch (btnID)
      {
      case 3:
        Serial.println("   → Action for Group2 Button 3");
        break;
      case 4:
        Serial.println("   → Action for Group2 Button 4");
        break;
      case 5:
        Serial.println("   → Action for Group2 Button 5");
        break;
      }
    }
  }
}

// ==================== SETUP ====================
void setup()
{
  Serial.begin(115200);

  // Create both queues
  queueGroup1 = xQueueCreate(10, sizeof(int));
  queueGroup2 = xQueueCreate(10, sizeof(int));

  // Create both tasks
  xTaskCreate(buttonTaskGroup1, "Group1Task", 2048, NULL, 10, NULL);
  xTaskCreate(buttonTaskGroup2, "Group2Task", 2048, NULL, 10, NULL);

  attachInterrupt(digitalPinToInterrupt(buttonPins[0]), button0_isr, FALLING);
  attachInterrupt(digitalPinToInterrupt(buttonPins[1]), button1_isr, FALLING);
  attachInterrupt(digitalPinToInterrupt(buttonPins[2]), button2_isr, FALLING);

  attachInterrupt(digitalPinToInterrupt(sensorPins[0]), button3_isr, FALLING);
  attachInterrupt(digitalPinToInterrupt(sensorPins[1]), button4_isr, FALLING);
  attachInterrupt(digitalPinToInterrupt(sensorPins[2]), button5_isr, FALLING);

  // Configure all pins (external pull-up)
  for (int i = 0; i < MAX_CHANNELS; i++)
  {
    pinMode(buttonPins[i], INPUT);
    pinMode(sensorPins[i], INPUT);
  }
  pinMode(CH1_RED_LED, OUTPUT);
  pinMode(CH1_GREEN_LED, OUTPUT);
  pinMode(CH1_LOCK, OUTPUT);
  pinMode(CH2_RED_LED, OUTPUT);
  pinMode(CH2_GREEN_LED, OUTPUT);
  pinMode(CH2_LOCK, OUTPUT);
  pinMode(CH3_RED_LED, OUTPUT);
  pinMode(CH3_GREEN_LED, OUTPUT);
  pinMode(CH3_LOCK, OUTPUT);

  Serial.println("✅ Setup complete.");
}

void loop()
{
  vTaskDelay(pdMS_TO_TICKS(1000)); // main loop sleeps
}