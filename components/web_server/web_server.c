#include "web_server.h"
#include <esp_http_server.h>
#include <esp_log.h>
#include <sys/param.h>

static const char *TAG = "WEB_SERVER";

static httpd_handle_t server = NULL;
static mash_schedule_t current_schedule;
static int is_running = 0;

// Simple HTML page to serve directly from C string
static const char* index_html = 
"<!DOCTYPE html>"
"<html><head><title>GIC3500 Controller</title>"
"<meta name='viewport' content='width=device-width, initial-scale=1'>"
"<style>"
"body { font-family: Arial, sans-serif; background: #222; color: #fff; padding: 20px; text-align: center; }"
".card { background: #333; border-radius: 10px; padding: 20px; margin-bottom: 20px; }"
"button { background: #5cb85c; color: white; border: none; padding: 10px 20px; font-size: 16px; border-radius: 5px; margin: 5px; cursor: pointer; }"
"button.stop { background: #d9534f; }"
"input { margin-bottom: 10px; padding: 5px; }"
"</style>"
"</head><body>"
"<h2>Brew Mashing Controller</h2>"
"<div class='card'>"
"<h3>Status</h3>"
"<p>Running: <span id='status_running'>No</span></p>"
"<p>Temperature: <span id='status_temp'>--</span> &deg;C</p>"
"<button onclick='start()'>Start</button>"
"<button class='stop' onclick='stop()'>Stop</button>"
"</div>"
"<div class='card'>"
"<h3>Schedule (T, t)</h3>"
"<form id='sched_form'>"
"1: Temp <input type=\"number\" id=\"t1\" value=\"65\"> Time <input type=\"number\" id=\"m1\" value=\"60\"><br>"
"2: Temp <input type=\"number\" id=\"t2\" value=\"75\"> Time <input type=\"number\" id=\"m2\" value=\"10\"><br>"
"3: Temp <input type=\"number\" id=\"t3\" value=\"78\"> Time <input type=\"number\" id=\"m3\" value=\"5\"><br>"
"<button type=\"button\" onclick='save()'>Save Schedule</button>"
"</form>"
"</div>"
"<script>"
"function start() { fetch('/api/start', {method: 'POST'}); }"
"function stop() { fetch('/api/stop', {method: 'POST'}); }"
"function save() {"
"  let s = {steps:[]};"
"  for(let i=1; i<=3; i++) {"
"    let t = document.getElementById('t'+i).value;"
"    let m = document.getElementById('m'+i).value;"
"    if (t > 10 && m > 0) s.steps.push({temp: parseFloat(t), hold_time_min: parseInt(m)});"
"  }"
"  fetch('/api/schedule', {method: 'POST', body: JSON.stringify(s)});"
"}"
"setInterval(() => {"
"  fetch('/api/status').then(r=>r.json()).then(d=>{"
"    document.getElementById('status_running').innerText = d.running ? 'Yes':'No';"
"    document.getElementById('status_temp').innerText = d.running ? d.target_temp : '--';"
"  });"
"}, 2000);"
"</script>"
"</body></html>";

static esp_err_t get_index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t post_start_handler(httpd_req_t *req) {
    is_running = 1;
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t post_stop_handler(httpd_req_t *req) {
    is_running = 0;
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// VERY basic JSON paring to avoid cJSON dependency for now, or we can add cJSON in CMake
// But esp-idf has cJSON built-in! So we can use it.
#include <cJSON.h>

static esp_err_t post_schedule_handler(httpd_req_t *req) {
    int total_len = req->content_len;
    int cur_len = 0;
    char *buf = malloc(total_len + 1);
    int received = 0;
    
    if (total_len >= 10000) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Content too long");
        return ESP_FAIL;
    }

    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive post data");
            free(buf);
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (root) {
        cJSON *steps = cJSON_GetObjectItem(root, "steps");
        if (steps && cJSON_IsArray(steps)) {
            int num = cJSON_GetArraySize(steps);
            if (num > MAX_MASH_STEPS) num = MAX_MASH_STEPS;
            current_schedule.num_steps = num;

            for (int i = 0; i < num; i++) {
                cJSON *item = cJSON_GetArrayItem(steps, i);
                if (item) {
                    cJSON *t = cJSON_GetObjectItem(item, "temp");
                    cJSON *m = cJSON_GetObjectItem(item, "hold_time_min");
                    if (t && m) {
                        current_schedule.steps[i].temp = t->valuedouble;
                        current_schedule.steps[i].hold_time_min = m->valueint;
                    }
                }
            }
        }
        cJSON_Delete(root);
        ESP_LOGI(TAG, "Parsed %d steps.", current_schedule.num_steps);
    }

    free(buf);
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t get_status_handler(httpd_req_t *req) {
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"running\":%d, \"target_temp\":%.2f}", 
             is_running, 
             current_schedule.num_steps > 0 ? current_schedule.steps[0].temp : 0.0);
             
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static httpd_uri_t uri_index = { .uri = "/", .method = HTTP_GET, .handler = get_index_handler, .user_ctx = NULL };
static httpd_uri_t uri_start = { .uri = "/api/start", .method = HTTP_POST, .handler = post_start_handler, .user_ctx = NULL };
static httpd_uri_t uri_stop = { .uri = "/api/stop", .method = HTTP_POST, .handler = post_stop_handler, .user_ctx = NULL };
static httpd_uri_t uri_schedule = { .uri = "/api/schedule", .method = HTTP_POST, .handler = post_schedule_handler, .user_ctx = NULL };
static httpd_uri_t uri_status = { .uri = "/api/status", .method = HTTP_GET, .handler = get_status_handler, .user_ctx = NULL };

void web_server_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10;

    current_schedule.num_steps = 0;

    ESP_LOGI(TAG, "Starting web server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &uri_index);
        httpd_register_uri_handler(server, &uri_start);
        httpd_register_uri_handler(server, &uri_stop);
        httpd_register_uri_handler(server, &uri_schedule);
        httpd_register_uri_handler(server, &uri_status);
    }
}

mash_schedule_t* get_current_schedule(void) {
    return &current_schedule;
}

int get_current_status(void) {
    return is_running;
}

void set_current_status(int running) {
    is_running = running;
}
