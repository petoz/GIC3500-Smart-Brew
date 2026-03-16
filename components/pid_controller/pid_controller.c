#include "pid_controller.h"

void pid_init(pid_context_t *ctx, float kp, float ki, float kd, float out_min, float out_max) {
    ctx->kp = kp;
    ctx->ki = ki;
    ctx->kd = kd;
    ctx->prev_error = 0.0f;
    ctx->integral = 0.0f;
    ctx->out_min = out_min;
    ctx->out_max = out_max;
}

float pid_compute(pid_context_t *ctx, float setpoint, float measured_val, float dt) {
    float error = setpoint - measured_val;
    float p_out = ctx->kp * error;

    ctx->integral += error * dt;
    float i_out = ctx->ki * ctx->integral;

    float derivative = (error - ctx->prev_error) / dt;
    float d_out = ctx->kd * derivative;

    float output = p_out + i_out + d_out;

    // Output limitation (Anti-Windup)
    if (output > ctx->out_max) {
        output = ctx->out_max;
        ctx->integral -= error * dt; // Simple anti-windup: do not integrate if saturated
    } else if (output < ctx->out_min) {
        output = ctx->out_min;
        ctx->integral -= error * dt;
    }

    ctx->prev_error = error;
    return output;
}

uint8_t pid_output_to_stage(float pid_output, float max_output_range, uint8_t max_stage) {
    if (pid_output <= 0.0f) {
        return 0; // Off
    }
    
    // Map pid_output [0, max_output_range] to [1, max_stage] proportionally
    float stage_f = (pid_output / max_output_range) * max_stage;
    
    // Round to nearest integer stage
    uint8_t stage = (uint8_t)(stage_f + 0.5f);

    // Limit to bounds
    if (stage < 1) stage = 1;
    if (stage > max_stage) stage = max_stage;

    return stage;
}
