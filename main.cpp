#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"

constexpr gpio_num_t STATUS_LED_PIN = GPIO_NUM_2;
constexpr UBaseType_t SENSOR_QUEUE_LEN = 10;

enum class SystemState {
    INIT,
    SYSTEM_OK,
    WARNING_DEGRADED,
    CRITICAL_ALERT,
    FAULT_INJECTED
};

struct SensorData {
    float temperature;
    float humidity;
    uint32_t timestamp_ms;
};

// Global FreeRTOS Handles & Shared Resources
static QueueHandle_t xSensorQueue = nullptr;
static SemaphoreHandle_t xStateMutex = nullptr;
static SystemState g_current_state = SystemState::INIT;

// Configurable Temperature Thresholds (°C)
static float g_temp_warn_threshold = 24.5f;
static float g_temp_crit_threshold = 26.0f;

// Thread-Safe State & Threshold Accessors
void setSystemState(SystemState new_state) {
    if (xSemaphoreTake(xStateMutex, portMAX_DELAY) == pdTRUE) {
        g_current_state = new_state;
        xSemaphoreGive(xStateMutex);
    }
}

SystemState getSystemState() {
    SystemState state = SystemState::INIT;
    if (xSemaphoreTake(xStateMutex, portMAX_DELAY) == pdTRUE) {
        state = g_current_state;
        xSemaphoreGive(xStateMutex);
    }
    return state;
}

void getThresholds(float &warn, float &crit) {
    if (xSemaphoreTake(xStateMutex, portMAX_DELAY) == pdTRUE) {
        warn = g_temp_warn_threshold;
        crit = g_temp_crit_threshold;
        xSemaphoreGive(xStateMutex);
    }
}

void setThresholds(float warn, float crit) {
    if (xSemaphoreTake(xStateMutex, portMAX_DELAY) == pdTRUE) {
        g_temp_warn_threshold = warn;
        g_temp_crit_threshold = crit;
        xSemaphoreGive(xStateMutex);
    }
}

const char* stateToString(SystemState state) {
    switch (state) {
        case SystemState::INIT:             return "INIT";
        case SystemState::SYSTEM_OK:        return "SYSTEM_OK";
        case SystemState::WARNING_DEGRADED: return "WARNING_DEGRADED";
        case SystemState::CRITICAL_ALERT:   return "CRITICAL_ALERT";
        case SystemState::FAULT_INJECTED:   return "FAULT_INJECTED";
        default:                            return "UNKNOWN";
    }
}

// Task 1: Simulated Sensor Reader (Producer)
void vSensorTask(void *pvParameters) {
    SensorData data{};
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for (;;) {
        data.temperature = 22.0f + (static_cast<float>(rand() % 50) / 10.0f);
        data.humidity = 45.0f + (static_cast<float>(rand() % 100) / 10.0f);
        data.timestamp_ms = pdTICKS_TO_MS(xTaskGetTickCount());

        if (xQueueSend(xSensorQueue, &data, 0) != pdPASS) {
            std::printf("[WARN] Sensor Queue Full! Dropping telemetry.\n");
        }

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));
    }
}

// Task 2: Data Processor & State Machine Engine (Consumer)
void vProcessingTask(void *pvParameters) {
    SensorData rx_data{};

    for (;;) {
        if (xQueueReceive(xSensorQueue, &rx_data, portMAX_DELAY) == pdTRUE) {
            SystemState current = getSystemState();

            // Evaluate state unless user explicitly injected a fault
            if (current != SystemState::FAULT_INJECTED) {
                float warn_th = 0.0f, crit_th = 0.0f;
                getThresholds(warn_th, crit_th);

                SystemState new_state = SystemState::SYSTEM_OK;
                if (rx_data.temperature >= crit_th) {
                    new_state = SystemState::CRITICAL_ALERT;
                } else if (rx_data.temperature >= warn_th) {
                    new_state = SystemState::WARNING_DEGRADED;
                }
                setSystemState(new_state);
            }

            SystemState active_state = getSystemState();

            // LED hardware control based on state
            if (active_state == SystemState::CRITICAL_ALERT || active_state == SystemState::FAULT_INJECTED) {
                gpio_set_level(STATUS_LED_PIN, 1);
            } else if (active_state == SystemState::WARNING_DEGRADED) {
                gpio_set_level(STATUS_LED_PIN, 1);
                vTaskDelay(pdMS_TO_TICKS(30));
                gpio_set_level(STATUS_LED_PIN, 0);
            } else {
                gpio_set_level(STATUS_LED_PIN, 0);
            }

            std::printf("[PROC Core %d] Time: %lums | Temp: %.1f C | State: [%s]\n",
                        xPortGetCoreID(), rx_data.timestamp_ms, rx_data.temperature, stateToString(active_state));
        }
    }
}

// Task 3: Interactive CLI Shell Task
void vCLITask(void *pvParameters) {
    char rx_buffer[64];
    size_t buf_idx = 0;

    std::printf("\n=== CLI Shell Ready. Type 'help' for commands. ===\n> ");

    for (;;) {
        while (Serial.available() > 0) {
            char c = static_cast<char>(Serial.read());
            if (c == '\r' || c == '\n') {
                if (buf_idx > 0) {
                    rx_buffer[buf_idx] = '\0';
                    std::printf("%s\n", rx_buffer);

                    if (std::strcmp(rx_buffer, "help") == 0) {
                        std::printf("Available Commands:\n");
                        std::printf("  status            - Query system state & RAM\n");
                        std::printf("  set_warn <val>    - Update warning threshold (e.g., set_warn 23.0)\n");
                        std::printf("  set_crit <val>    - Update critical threshold (e.g., set_crit 25.0)\n");
                        std::printf("  fault             - Inject artificial fault\n");
                        std::printf("  clear             - Clear fault state\n");
                    } else if (std::strcmp(rx_buffer, "status") == 0) {
                        float w = 0.0f, c_val = 0.0f;
                        getThresholds(w, c_val);
                        std::printf("State: %s | Free Heap: %zu bytes | Warn: %.1f C | Crit: %.1f C\n",
                                    stateToString(getSystemState()), xPortGetFreeHeapSize(), w, c_val);
                    } else if (std::strncmp(rx_buffer, "set_warn ", 9) == 0) {
                        float new_w = std::atof(rx_buffer + 9);
                        float w = 0.0f, c_val = 0.0f;
                        getThresholds(w, c_val);
                        setThresholds(new_w, c_val);
                        std::printf("[CLI] Warning threshold set to %.1f C\n", new_w);
                    } else if (std::strncmp(rx_buffer, "set_crit ", 9) == 0) {
                        float new_c = std::atof(rx_buffer + 9);
                        float w = 0.0f, c_val = 0.0f;
                        getThresholds(w, c_val);
                        setThresholds(w, new_c);
                        std::printf("[CLI] Critical threshold set to %.1f C\n", new_c);
                    } else if (std::strcmp(rx_buffer, "fault") == 0) {
                        setSystemState(SystemState::FAULT_INJECTED);
                        std::printf("[CLI] Artificial FAULT injected.\n");
                    } else if (std::strcmp(rx_buffer, "clear") == 0) {
                        setSystemState(SystemState::SYSTEM_OK);
                        std::printf("[CLI] Fault cleared.\n");
                    } else {
                        std::printf("Unknown command: '%s'\n", rx_buffer);
                    }

                    buf_idx = 0;
                    std::printf("> ");
                }
            } else if (buf_idx < sizeof(rx_buffer) - 1) {
                rx_buffer[buf_idx++] = c;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void setup() {
    Serial.begin(115200);

    gpio_reset_pin(STATUS_LED_PIN);
    gpio_set_direction(STATUS_LED_PIN, GPIO_MODE_OUTPUT);

    xSensorQueue = xQueueCreate(SENSOR_QUEUE_LEN, sizeof(SensorData));
    xStateMutex = xSemaphoreCreateMutex();

    if (xSensorQueue == nullptr || xStateMutex == nullptr) {
        std::printf("Fatal: Failed to create FreeRTOS primitives\n");
        return;
    }

    setSystemState(SystemState::SYSTEM_OK);

    xTaskCreatePinnedToCore(vSensorTask, "SensorTask", 2048, nullptr, 2, nullptr, 0);
    xTaskCreatePinnedToCore(vProcessingTask, "ProcTask", 2048, nullptr, 2, nullptr, 1);
    xTaskCreatePinnedToCore(vCLITask, "CLITask", 3072, nullptr, 1, nullptr, 0);

    std::printf("FreeRTOS Virtual Node with Interactive CLI Initialized.\n");
}

void loop() {
    vTaskDelay(portMAX_DELAY);
}