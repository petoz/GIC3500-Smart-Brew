#ifndef WIFI_MQTT_H
#define WIFI_MQTT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void wifi_mqtt_init(void);
bool is_mqtt_connected(void);
void mqtt_publish_status(float current_temp, float target_temp, int stage, int phase, int time_left_s);
float get_latest_temperature(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_MQTT_H
