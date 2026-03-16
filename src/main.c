#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

// Component headers
#include "power_control.h"
#include "wifi_mqtt.h"
#include "pid_controller.h"
#include "web_server.h"

static const char *TAG = "MAIN";

// Mashing States
typedef enum {
    STATE_IDLE,
    STATE_HEATING,
    STATE_HOLDING,
    STATE_DONE
} mash_state_t;

static mash_state_t current_state = STATE_IDLE;
static int current_step_index = 0;
static uint32_t hold_start_time = 0; // seconds

void mashing_task(void *pvParameters) {
    pid_context_t pid;
    // PID tuning parameters - will need adjustment for 3.5kW boiler
    // High max out of 100 for percentage
    pid_init(&pid, 5.0f, 0.1f, 10.0f, 0.0f, 100.0f);

    int last_stage = 0;
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000)); // Run loop every 2 seconds

        mash_schedule_t* schedule = get_current_schedule();
        int running = get_current_status();
        float current_temp = get_latest_temperature();
        
        if (!running) {
            if (current_state != STATE_IDLE) {
                ESP_LOGI(TAG, "Mashing stopped. Turning off power.");
                power_control_set_stage(0);
                last_stage = 0;
                current_state = STATE_IDLE;
            }
            mqtt_publish_status(current_temp, 0.0, 0, STATE_IDLE, 0);
            continue;
        }

        // If running but no valid schedule, stop it
        if (schedule->num_steps == 0) {
            ESP_LOGW(TAG, "Running but schedule is empty. Stopping.");
            set_current_status(0);
            continue;
        }
        
        // If current temp is invalid (not received yet), power off but stay in running state
        if (current_temp < 0.0f) {
            ESP_LOGW(TAG, "Waiting for valid temperature via MQTT...");
            power_control_set_stage(0);
            last_stage = 0;
            continue;
        }

        if (current_state == STATE_IDLE && running) {
            ESP_LOGI(TAG, "Mashing started!");
            current_state = STATE_HEATING;
            current_step_index = 0;
            pid.integral = 0.0f; // reset PID
        }

        float target_temp = schedule->steps[current_step_index].temp;
        int time_left_s = 0;

        float pid_out = pid_compute(&pid, target_temp, current_temp, 2.0f); // dt = 2s
        uint8_t stage = pid_output_to_stage(pid_out, 100.0f, 11);

        switch (current_state) {
            case STATE_HEATING:
                // Fast heat up if temp difference is huge (e.g. > 5 deg) -> force stage 11
                if (target_temp - current_temp > 5.0f) {
                    stage = 11;
                } else {
                    // Start capping output to max stage 5 to avoid overshoot
                    stage = pid_output_to_stage(pid_out, 100.0f, 5); 
                }

                if (current_temp >= target_temp - 0.2f) {
                    ESP_LOGI(TAG, "Target temp reached! Moving to HOLDING.");
                    current_state = STATE_HOLDING;
                    hold_start_time = esp_timer_get_time() / 1000000ULL; // seconds
                    pid.integral = 0.0f; // Reset windup when entering hold
                }
                break;

            case STATE_HOLDING:
                {
                    // Restrict max holding output to stage 3-5 to stabilize 36L 3.5kW
                    stage = pid_output_to_stage(pid_out, 100.0f, 4);

                    uint32_t now = esp_timer_get_time() / 1000000ULL;
                    uint32_t elapsed = now - hold_start_time;
                    int hold_target_s = schedule->steps[current_step_index].hold_time_min * 60;
                    time_left_s = hold_target_s - elapsed;

                    if (time_left_s <= 0) {
                        time_left_s = 0;
                        ESP_LOGI(TAG, "Hold step %d complete.", current_step_index + 1);
                        current_step_index++;
                        if (current_step_index >= schedule->num_steps) {
                            ESP_LOGI(TAG, "All steps completed!");
                            current_state = STATE_DONE;
                        } else {
                            current_state = STATE_HEATING;
                            pid.integral = 0.0f;
                        }
                    }
                }
                break;

            case STATE_DONE:
                stage = 0;
                set_current_status(0); // auto-stop
                current_state = STATE_IDLE;
                break;
                
            default:
                break;
        }

        if (stage != last_stage || current_state == STATE_HEATING || current_state == STATE_HOLDING) {
            // Re-apply stage periodically or when changed
            power_control_set_stage(stage);
            last_stage = stage;
        }

        mqtt_publish_status(current_temp, target_temp, stage, current_state, time_left_s);
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting Mashing Controller (ESP-IDF)");

    wifi_mqtt_init();
    power_control_init();
    web_server_start();

    // Start background task for control loop (pin to core 1 if available, otherwise 0)
    xTaskCreate(mashing_task, "mashing_task", 4096, NULL, 5, NULL);
}
