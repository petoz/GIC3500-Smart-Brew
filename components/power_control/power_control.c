#include "power_control.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "POWER_CTRL";

// Multiplexer pins
#define PIN_S0 GPIO_NUM_23
#define PIN_S1 GPIO_NUM_1
#define PIN_S2 GPIO_NUM_2
#define PIN_S3 GPIO_NUM_21
#define PIN_EN GPIO_NUM_22 // Active LOW

#define PULSE_MS 200

static void mux_enable(void) {
    gpio_set_level(PIN_EN, 0);
}

static void mux_disable(void) {
    gpio_set_level(PIN_EN, 1);
}

static void set_mux_channel(uint8_t channel) {
    gpio_set_level(PIN_S0, (channel >> 0) & 0x01);
    gpio_set_level(PIN_S1, (channel >> 1) & 0x01);
    gpio_set_level(PIN_S2, (channel >> 2) & 0x01);
    gpio_set_level(PIN_S3, (channel >> 3) & 0x01);
}

void power_control_init(void) {
    ESP_LOGI(TAG, "Initializing power control GPIOs");

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << PIN_S0) | (1ULL << PIN_S1) | (1ULL << PIN_S2) | (1ULL << PIN_S3) | (1ULL << PIN_EN),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    gpio_config(&io_conf);

    mux_disable();
    set_mux_channel(0);

    // Initial pulse to reset/select 0 stage
    power_control_set_stage(0);
}

void power_control_set_stage(uint8_t stage) {
    if (stage > 11) {
        stage = 11;
    }
    
    ESP_LOGI(TAG, "Setting power stage to %d", stage);

    mux_disable();
    vTaskDelay(pdMS_TO_TICKS(20));

    set_mux_channel(stage);
    vTaskDelay(pdMS_TO_TICKS(20));

    mux_enable();
    vTaskDelay(pdMS_TO_TICKS(PULSE_MS));

    mux_disable();
}
