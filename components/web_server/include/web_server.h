#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

// Shared data structures for mashing schedule
#define MAX_MASH_STEPS 6

typedef struct {
    float temp;
    int hold_time_min;
} mash_step_t;

typedef struct {
    mash_step_t steps[MAX_MASH_STEPS];
    int num_steps;
} mash_schedule_t;

void web_server_start(void);
mash_schedule_t* get_current_schedule(void);
int get_current_status(void); // 0 = IDLE, 1 = RUNNING
void set_current_status(int running);

int get_manual_stage(void); // Returns -1 for AUTO mode, 0-11 for manual
void set_manual_stage(int stage);

#ifdef __cplusplus
}
#endif

#endif // WEB_SERVER_H
