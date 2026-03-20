#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

// Component headers
#include "mdns.h"
#include "power_control.h"
#include "web_server.h"
#include "wifi_mqtt.h"

static const char *TAG = "MAIN";

// Global exposure for Web UI Telemetry
static float global_current_temp = 0.0f;
static uint8_t global_active_stage = 0;
static int global_time_left_s = 0;

float get_global_current_temp(void) { return global_current_temp; }
uint8_t get_global_active_stage(void) { return global_active_stage; }
int get_global_time_left_s(void) { return global_time_left_s; }

// Current active mash step index (for correct target temp display)
static int global_step_index = 0;
int get_global_step_index(void) { return global_step_index; }

// Mashing States
typedef enum {
  STATE_IDLE,
  STATE_HEATING,
  STATE_HOLDING,
  STATE_DONE,
  STATE_MANUAL
} mash_state_t;

static mash_state_t current_state = STATE_IDLE;
static int current_step_index = 0;
static uint32_t hold_start_time = 0; // seconds

void mashing_task(void *pvParameters) {
  int last_stage = 0;
  uint32_t last_stage_change_ms = 0;
  float previous_temp = -1.0f;

  while (1) {
    control_config_t *cfg = get_control_config();
    uint32_t interval_ms =
        (cfg && cfg->control_interval_ms > 0) ? cfg->control_interval_ms : 5000;
    vTaskDelay(pdMS_TO_TICKS(interval_ms));

    uint32_t now_ms = esp_timer_get_time() / 1000ULL;

    mash_schedule_t *schedule = get_current_schedule();
    int running = get_current_status();
    float current_temp = get_latest_temperature();
    int manual_stage = get_manual_stage();

    if (manual_stage >= 0 && manual_stage <= 11) {
      if (current_state != STATE_MANUAL) {
        ESP_LOGI(TAG, "Entering Manual Mode! Stage: %d", manual_stage);
        current_state = STATE_MANUAL;
        set_current_status(0); // Stop auto schedule when manual starts
      }
      if (last_stage != manual_stage) {
        power_control_set_stage(manual_stage);
        last_stage = manual_stage;
      }
      mqtt_publish_status(current_temp, 0.0, manual_stage, STATE_MANUAL, 0);
      continue;
    } else if (current_state == STATE_MANUAL) {
      ESP_LOGI(TAG, "Exiting Manual Mode.");
      power_control_set_stage(0);
      last_stage = 0;
      current_state = STATE_IDLE;
    }

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

    // If current temp is invalid (not received yet), power off but stay in
    // running state
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
      last_stage_change_ms = 0; // reset
    }

    float target_temp = schedule->steps[current_step_index].temp;
    int time_left_s = 0;

    // Calculate slope per second (just a basic differential metric)
    float slope = 0.0f;
    if (previous_temp > 0.0f) {
      slope = (current_temp - previous_temp) / (interval_ms / 1000.0f);
    }
    previous_temp = current_temp;

    uint8_t target_stage = last_stage;

    switch (current_state) {
    case STATE_HEATING: {
      float diff = target_temp - current_temp;

      // Proceed to stage transitions if we hit target bounds
      if (current_temp >= target_temp - cfg->hysteresis_down) {
        ESP_LOGI(TAG, "Target temp reached! Moving to HOLDING.");
        current_state = STATE_HOLDING;
        hold_start_time = now_ms / 1000;
        target_stage = 3; // start hold gently
      } else if ((now_ms - last_stage_change_ms) >= cfg->heating_min_hold_ms) {
        if (diff > 3.0f) {
          target_stage = 11;
        } else if (diff > 2.0f) {
          target_stage = 6;
        } else if (diff > 1.0f) {
          target_stage = 3;
        } else if (diff > 0.5f) {
          target_stage = (slope > 0.1f) ? 3 : 4;
        } else {
          target_stage = (slope > 0.05f) ? 0 : 1; // Soft approach
        }
      }
    } break;

    case STATE_HOLDING: {
      uint32_t now_sec = now_ms / 1000;
      uint32_t elapsed = now_sec - hold_start_time;
      int hold_target_s =
          schedule->steps[current_step_index].hold_time_min * 60;
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
          last_stage_change_ms = 0;
        }
      } else if ((now_ms - last_stage_change_ms) >= cfg->holding_min_hold_ms) {
        // Hysteresis window bounds
        if (current_temp < (target_temp - cfg->hysteresis_down)) {
          target_stage = 3; // Turn on heat
        } else if (current_temp > (target_temp + cfg->hysteresis_up)) {
          target_stage = 0; // Turn off heat
        } else {
          // Maintain gentle floating heat inside window based on trend
          if (slope < -0.01f)
            target_stage = 2;
          else if (slope > 0.01f)
            target_stage = 0;
          else
            target_stage = 1;
        }

        // Enforce hold cap max 3
        if (target_stage > 3)
          target_stage = 3;
      }
    } break;

    case STATE_DONE:
      target_stage = 0;
      set_current_status(0); // auto-stop
      current_state = STATE_IDLE;
      break;

    default:
      break;
    }

    if (target_stage != last_stage) {
      ESP_LOGI(TAG,
               "Stage changed: %d -> %d (Current Temp: %.2f, Target: %.2f)",
               last_stage, target_stage, current_temp, target_temp);
      power_control_set_stage(target_stage);
      last_stage = target_stage;
      last_stage_change_ms = now_ms;
    }

    mqtt_publish_status(current_temp, target_temp, last_stage, current_state,
                        time_left_s);

    // Update global exposure state for Web UI
    global_current_temp = current_temp;
    global_active_stage = last_stage;
    global_time_left_s = time_left_s;
    global_step_index = current_step_index;
  }
}

void app_main(void) {
  ESP_LOGI(TAG, "Starting Mashing Controller (ESP-IDF)");

  // Start mDNS responder - accessible as http://gic3500.local/
  esp_err_t mdns_err = mdns_init();
  if (mdns_err == ESP_OK) {
    mdns_hostname_set("gic3500");
    mdns_instance_name_set("GIC3500 Smart Brew");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "mDNS started: http://gic3500.local/");
  } else {
    ESP_LOGE(TAG, "mDNS init failed: %d", mdns_err);
  }

  wifi_mqtt_init();
  power_control_init();
  web_server_start();

  // Start background task for control loop (pin to core 1 if available,
  // otherwise 0)
  xTaskCreate(mashing_task, "mashing_task", 4096, NULL, 5, NULL);
}
