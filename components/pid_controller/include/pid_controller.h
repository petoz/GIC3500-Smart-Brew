#ifndef PID_CONTROLLER_H
#define PID_CONTROLLER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float kp;
    float ki;
    float kd;
    float prev_error;
    float integral;
    float out_min;
    float out_max;
} pid_context_t;

void pid_init(pid_context_t *ctx, float kp, float ki, float kd, float out_min, float out_max);
float pid_compute(pid_context_t *ctx, float setpoint, float measured_val, float dt);
uint8_t pid_output_to_stage(float pid_output, float max_output_range, uint8_t max_stage);

#ifdef __cplusplus
}
#endif

#endif // PID_CONTROLLER_H
