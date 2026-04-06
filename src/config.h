#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// ESP32 DECONTAMINATION CHAMBER - CONFIGURATION FILE
// ============================================================================
// Board: ESP32 (4MB Flash)
// Framework: PlatformIO
// Programming: FreeRTOS
// Serial: 115200 baud
// Mode Selection: Runtime switchable via Serial Monitor / TFT (future)
// ============================================================================

// ============================================================================
// 0. FIRMWARE VERSION
// ============================================================================
#define FIRMWARE_VERSION "1.0.0"
#define FIRMWARE_NAME "ESP32 Decontamination Chamber"

// ============================================================================
// 1. DEBUG MODE
// ============================================================================
#define DEBUG_MODE 1  // Set to 1 for verbose serial logging, 0 to disable

// ============================================================================
// 2. SERIAL CONFIGURATION
// ============================================================================
#define SERIAL_BAUD 115200

// ============================================================================
// 3. OPERATING MODE DEFINITIONS
// ============================================================================
#define MODE_A 1
#define MODE_B 2
#define MODE_C 3

#define DEFAULT_MODE MODE_A  // System starts in MODE_A on boot/reset

// Runtime mode variable (will be declared in main.cpp)
// extern int current_mode;  // Can be changed via Serial Monitor / TFT

// ============================================================================
// 4. MODE PROPERTIES
// ============================================================================

// Number of channels used per mode
#define CHANNELS_MODE_A 2  // Channels 1 & 2
#define CHANNELS_MODE_B 2  // Channels 1 & 2
#define CHANNELS_MODE_C 3  // Channels 1, 2, & 3

// Maximum channels in any mode
#define MAX_CHANNELS 3

// ============================================================================
// 5. CHANNEL 1 GPIO PIN MAPPING
// ============================================================================
#define CH1_PUSH_BTN    34   // Push Button (GPIO34, active-LOW pull-up)
#define CH1_SENSOR      35   // Door Closed Sensor (GPIO35, active-LOW pull-up)
#define CH1_RED_LED     32   // Red LED (GPIO32)
#define CH1_GREEN_LED   33   // Green LED (GPIO33)
#define CH1_LOCK  27   // Door Lock Relay (GPIO27, active-HIGH)

// ============================================================================
// 6. CHANNEL 2 GPIO PIN MAPPING
// ============================================================================
#define CH2_PUSH_BTN    36   // Push Button (GPIO36, active-LOW pull-up)
#define CH2_SENSOR      39   // Door Closed Sensor (GPIO39, active-LOW pull-up)
#define CH2_RED_LED     32   // Red LED (GPIO32) - shared with CH1
#define CH2_GREEN_LED   15   // Green LED (GPIO15)
#define CH2_LOCK  16   // Door Lock Relay (GPIO16, active-HIGH)

// ============================================================================
// 7. CHANNEL 3 GPIO PIN MAPPING (MODE C Only - Ignored in MODE A/B)
// ============================================================================
#define CH3_PUSH_BTN    13   // Push Button (GPIO13, active-LOW pull-up)
#define CH3_SENSOR      14   // Door Closed Sensor (GPIO14, active-LOW pull-up)
#define CH3_RED_LED     32   // Red LED (GPIO32) - shared with CH1
#define CH3_GREEN_LED   25   // Green LED (GPIO25)
#define CH3_LOCK  26   // Door Lock Relay (GPIO26, active-HIGH)

// ============================================================================
// 8. GLOBAL GPIO PIN MAPPING
// ============================================================================
#define EMERGENCY_BTN_PIN   5    // Emergency Button (GPIO5, active-LOW pull-up)
#define RELAY4_SPRAY_PIN    22   // Decontamination/Spray Relay (GPIO22, active-HIGH)
#define AUX_LIGHT_PIN       21   // Auxiliary Light (GPIO21)

// ============================================================================
// 9. GPIO LOGIC DEFINITIONS
// ============================================================================

// Relay Logic (Active-HIGH: HIGH = locked/ON, LOW = unlocked/OFF)
#define RELAY_ON            HIGH
#define RELAY_OFF           LOW
#define RELAY_LOCK          HIGH   // Door lock energized
#define RELAY_UNLOCK        LOW    // Door lock de-energized
#define RELAY_SPRAY_ON      HIGH   // Spray relay ON
#define RELAY_SPRAY_OFF     LOW    // Spray relay OFF

// Button/Sensor Logic (Active-LOW: LOW = pressed/closed, HIGH = released/open)
#define BUTTON_PRESSED      LOW
#define BUTTON_RELEASED     HIGH
#define DOOR_CLOSED       LOW
#define DOOR_OPEN         HIGH


// ============================================================================
// 10. TIMING CONSTANTS (in milliseconds)
// ============================================================================

// Auto Re-lock Safety Timeout
// If door not confirmed closed within this time, system waits indefinitely
#define AUTO_RELOCK_TIMEOUT        10000    // 10 seconds

// Decontamination (Spray) Process Duration
// Valid for MODE A (forward), MODE B (both directions), MODE C (forward)
#define SPRAY_DURATION             15000    // 15 seconds

// Emergency Mode Timeout
// After emergency triggered, system locks all doors and returns to IDLE
#define EMERGENCY_TIMEOUT          10000    // 10 seconds

// Window Close Timeout
// Time allowed for users to exit after spray completes before re-locking
#define WINDOW_CLOSE_TIMEOUT        20000    // 20 seconds

// System Failure Timeout
// After critical failure (e.g. door opened during spray), system locks all doors and returns to IDLE
#define SYSTEM_FAILURE_TIMEOUT      1500    // 1.5 seconds

// Speed Door Timeout
#define SPEED_DOOR_TIMEOUT          3000    // 3 seconds

// Button Debounce Delay
// Button must be stable (LOW) for this duration to register as pressed
#define BTN_DEBOUNCE_TIME          150       // 50 milliseconds

// Sensor Debounce Delay
// Door sensor must be stable (LOW) for this duration to confirm closed
#define SENSOR_DEBOUNCE_TIME       200      // 200 milliseconds

// Main Loop Execution Interval
// How often main loop runs (non-blocking)
#define MAIN_LOOP_DELAY            10       // 10 milliseconds

// ============================================================================
// 11. FEATURE FLAGS
// ============================================================================

// Enable/Disable Serial Debug Output for I/O operations
#define DEBUG_IO_OPERATIONS        1

// Enable/Disable Serial Debug Output for Timer operations
#define DEBUG_TIMER_OPERATIONS     1

// Enable/Disable Serial Debug Output for Logic/Mode operations
#define DEBUG_LOGIC_OPERATIONS     1

// Enable/Disable Serial Debug Output for Button/Sensor debouncing
#define DEBUG_DEBOUNCE_OPERATIONS  1  // Set to 1 for detailed debounce logs

// Enable/Disable Serial command interface for mode switching
#define ENABLE_SERIAL_MODE_SELECT  1  // Set to 0 if using TFT only

// ============================================================================
#endif  // CONFIG_H
// ============================================================================