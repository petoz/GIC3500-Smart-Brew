#include "web_server.h"
#include <esp_http_server.h>
#include <esp_log.h>
#include <sys/param.h>
#include <cJSON.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"

static const char *TAG = "WEB_SERVER";

static httpd_handle_t server = NULL;
static mash_schedule_t current_schedule;
static int is_running = 0;
static int current_manual_stage = -1; // -1 means AUTO mode

static control_config_t current_config = {
    .heating_min_hold_ms = 10000,
    .holding_min_hold_ms = 30000,
    .control_interval_ms = 5000,
    .hysteresis_up = 0.3f,
    .hysteresis_down = 0.5f
};

control_config_t* get_control_config(void) {
    return &current_config;
}

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
"<p>Target Temp: <span id='status_temp'>--</span> &deg;C</p>"
"<p>Actual Temp: <span id='actual_temp'>--</span> &deg;C</p>"
"<p>Power Stage: <span id='active_stage'>--</span></p>"
"<p>Time Left: <span id='time_left'>--</span> s</p>"
"<button onclick='start()'>Start</button>"
"<button class='stop' onclick='stop()'>Stop</button>"
"</div>"
"<div class='card'>"
"<h3>Schedule (T, t)</h3>"
"<form id='sched_form'>"
"<div id='steps_container'></div>"
"<button type='button' onclick='addStep()'>+ Add Step</button> "
"<button type='button' onclick='remStep()'>- Remove Step</button><br><br>"
"<button type=\"button\" onclick='save()'>Save Schedule</button>"
"</form>"
"</div>"
"<div class='card'>"
"<h3>Manual Mode</h3>"
"Stage (0-11): <input type='number' id='manual_stage' min='0' max='11' value='0'><br>"
"<button onclick='setManual()'>Set Stage</button>"
"<button class='stop' onclick='stopManual()'>Resume Auto</button>"
"</div>"
"<div class='card'>"
"<h3>Advanced Settings</h3>"
"Heating hold (ms): <input type='number' id='h_hold' style='width:80px'> "
"Holding hold (ms): <input type='number' id='o_hold' style='width:80px'><br>"
"Control interval (ms): <input type='number' id='c_int' style='width:80px'><br>"
"Hysteresis Up: <input type='number' step='0.1' id='h_up' style='width:60px'> "
"Down: <input type='number' step='0.1' id='h_dn' style='width:60px'><br>"
"<button onclick='saveAdvanced()'>Save Advanced</button>"
"</div>"
"<div class='card'>"
"<h3>System Config</h3>"
"MQTT Logs: <span id='status_log'>--</span> "
"<button onclick='toggleLog()'>Toggle Logs</button><br><br>"
"<button type=\"button\" onclick='setupWifi()'>Update WiFi & MQTT</button><br><br>"
"<h4>Firmware Update</h4>"
"<input type='file' id='ota_file' accept='.bin'>"
"<button onclick='uploadOta()'>Upload & Flash</button>"
"</div>"
"<script>"
"function start() { fetch('/api/start', {method: 'POST'}); }"
"function stop() { fetch('/api/stop', {method: 'POST'}); }"
"let stepCount = 3;"
"function renderSteps(data) {"
"  let h = '';"
"  for(let i=1; i<=stepCount; i++) {"
"    let temp = (data && data[i-1]) ? data[i-1].temp : 65.0;"
"    let time = (data && data[i-1]) ? data[i-1].time : 60;"
"    h += i + ': Temp &deg;C <input type=\"number\" id=\"t'+i+'\" value=\"' + temp + '\" style=\"width:60px\"> ';"
"    h += 'Time (m) <input type=\"number\" id=\"m'+i+'\" value=\"' + time + '\" style=\"width:60px\"><br>';"
"  }"
"  document.getElementById('steps_container').innerHTML = h;"
"}"
"fetch('/api/schedule').then(r=>r.json()).then(d=>{"
"  if(d && d.steps && d.steps.length > 0) {"
"    stepCount = d.steps.length;"
"    renderSteps(d.steps);"
"  } else { renderSteps(null); }"
"}).catch(()=>renderSteps(null));"
"function addStep() { if(stepCount<5) {stepCount++; renderSteps(null);} }"
"function remStep() { if(stepCount>1) {stepCount--; renderSteps(null);} }"
"function save() {"
"  let s = {steps:[]};"
"  for(let i=1; i<=stepCount; i++) {"
"    let t = document.getElementById('t'+i).value;"
"    let m = document.getElementById('m'+i).value;"
"    if (t > 10 && m > 0) s.steps.push({temp: parseFloat(t), hold_time_min: parseInt(m)});"
"  }"
"  fetch('/api/schedule', {method: 'POST', body: JSON.stringify(s)});"
"}"
"function setManual() {"
"  let v = parseInt(document.getElementById('manual_stage').value);"
"  if (v >= 0 && v <= 11) fetch('/api/manual', {method:'POST', body: JSON.stringify({stage: v})});"
"}"
"function saveAdvanced() {"
"  let a = {"
"    h_hold: parseInt(document.getElementById('h_hold').value),"
"    o_hold: parseInt(document.getElementById('o_hold').value),"
"    c_int: parseInt(document.getElementById('c_int').value),"
"    h_up: parseFloat(document.getElementById('h_up').value),"
"    h_dn: parseFloat(document.getElementById('h_dn').value)"
"  };"
"  fetch('/api/advanced', {method: 'POST', body: JSON.stringify(a)}).then(()=>alert('Saved Advanced!'));"
"}"
"function toggleLog() { fetch('/api/log', {method:'POST'}); }"
"function setupWifi() {"
"  let w = {"
"    ssid: prompt('New WiFi SSID:'),"
"    pass: prompt('New WiFi Password:'),"
"    mqtt: prompt('MQTT Broker IP:'),"
"    muser: prompt('MQTT Username (optional):'),"
"    mpass: prompt('MQTT Password (optional):')"
"  };"
"  if (w.ssid && w.mqtt) fetch('/api/config', {method: 'POST', body: JSON.stringify(w)}).then(()=>alert('Rebooting...'));"
"}"
"function uploadOta() {"
"  let f = document.getElementById('ota_file').files[0];"
"  if(!f) return alert('Select a .bin file');"
"  fetch('/api/ota', {method: 'POST', body: f}).then(r=>{"
"    if(r.ok) { alert('Success! Rebooting...'); setTimeout(()=>location.reload(), 5000); }"
"    else alert('OTA Failed');"
"  });"
"}"
"setInterval(() => {"
"  fetch('/api/status').then(r=>r.json()).then(d=>{"
"    document.getElementById('status_running').innerText = d.running ? 'Yes':'No';"
"    document.getElementById('status_temp').innerText = d.running ? d.target_temp : '--';"
"    document.getElementById('actual_temp').innerText = d.current_temp !== undefined ? parseFloat(d.current_temp).toFixed(2) : '--';"
"    document.getElementById('active_stage').innerText = d.active_stage !== undefined ? d.active_stage : '--';"
"    document.getElementById('time_left').innerText = d.time_left_s !== undefined ? d.time_left_s : '--';"
"    document.getElementById('status_log').innerText = d.mqtt_log ? 'ON':'OFF';"
"    if(d.h_hold && !document.getElementById('h_hold').value) {"
"      document.getElementById('h_hold').value = d.h_hold;"
"      document.getElementById('o_hold').value = d.o_hold;"
"      document.getElementById('c_int').value  = d.c_int;"
"      document.getElementById('h_up').value   = d.h_up;"
"      document.getElementById('h_dn').value   = d.h_dn;"
"    }"
"    if(d.manual_stage >= 0) { "
"      document.getElementById('status_running').innerText = 'MANUAL (Stage ' + d.manual_stage + ')';"
"    }"
"  }).catch(e=>console.log(e));"
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

// Built-in cJSON parsing is included at the top

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
        
        nvs_handle_t my_handle;
        if (nvs_open("gic3500_cfg", NVS_READWRITE, &my_handle) == ESP_OK) {
            nvs_set_blob(my_handle, "schedule", &current_schedule, sizeof(mash_schedule_t));
            nvs_commit(my_handle);
            nvs_close(my_handle);
            ESP_LOGI(TAG, "Saved schedule to NVS.");
        }
    }

    free(buf);
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t get_schedule_handler(httpd_req_t *req) {
    char buf[512];
    int offset = snprintf(buf, sizeof(buf), "{\"steps\":[");
    for (int i = 0; i < current_schedule.num_steps; i++) {
        offset += snprintf(buf + offset, sizeof(buf) - offset, "{\"temp\":%.2f, \"time\":%d}%s", 
            current_schedule.steps[i].temp, current_schedule.steps[i].hold_time_min,
            i == current_schedule.num_steps - 1 ? "" : ",");
    }
    snprintf(buf + offset, sizeof(buf) - offset, "]}");
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t post_manual_handler(httpd_req_t *req) {
    int total_len = req->content_len;
    if (total_len >= 1000) return ESP_FAIL;
    char *buf = malloc(total_len + 1);
    if(httpd_req_recv(req, buf, total_len) <= 0) { free(buf); return ESP_FAIL; }
    buf[total_len] = '\0';
    
    cJSON *root = cJSON_Parse(buf);
    if (root) {
        cJSON *s = cJSON_GetObjectItem(root, "stage");
        if (s) {
            current_manual_stage = s->valueint;
            ESP_LOGI(TAG, "Manual stage set to: %d", current_manual_stage);
        }
        cJSON_Delete(root);
    }
    
    free(buf);
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

extern void enable_mqtt_logging(bool enable);
extern bool get_mqtt_logging(void);

static esp_err_t post_log_handler(httpd_req_t *req) {
    bool current = get_mqtt_logging();
    enable_mqtt_logging(!current);
    
    // Save to NVS
    nvs_handle_t my_handle;
    if (nvs_open("gic3500_cfg", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_i32(my_handle, "mqtt_log", !current ? 1 : 0);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    }
    
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

extern float get_global_current_temp(void);
extern uint8_t get_global_active_stage(void);
extern int get_global_time_left_s(void);
extern int get_global_step_index(void);

static esp_err_t get_status_handler(httpd_req_t *req) {
    char buf[300];
    int step = get_global_step_index();
    float target = (current_schedule.num_steps > 0 && step < current_schedule.num_steps)
                   ? current_schedule.steps[step].temp : 0.0f;
    snprintf(buf, sizeof(buf), 
             "{\"running\":%d, \"target_temp\":%.2f, \"current_temp\":%.2f, \"active_stage\":%d, \"time_left_s\":%d, \"manual_stage\":%d, \"mqtt_log\":%d," 
             "\"h_hold\":%d, \"o_hold\":%d, \"c_int\":%d, \"h_up\":%.2f, \"h_dn\":%.2f}", 
             is_running, 
             target,
             get_global_current_temp(), get_global_active_stage(), get_global_time_left_s(),
             current_manual_stage,
             get_mqtt_logging() ? 1 : 0,
             current_config.heating_min_hold_ms, current_config.holding_min_hold_ms,
             current_config.control_interval_ms, current_config.hysteresis_up, current_config.hysteresis_down);
             
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t post_advanced_handler(httpd_req_t *req) {
    int total_len = req->content_len;
    if (total_len >= 1000) return ESP_FAIL;
    char *buf = malloc(total_len + 1);
    if(httpd_req_recv(req, buf, total_len) <= 0) { free(buf); return ESP_FAIL; }
    buf[total_len] = '\0';
    
    cJSON *root = cJSON_Parse(buf);
    if (root) {
        cJSON *h = cJSON_GetObjectItem(root, "h_hold");
        cJSON *o = cJSON_GetObjectItem(root, "o_hold");
        cJSON *c = cJSON_GetObjectItem(root, "c_int");
        cJSON *up = cJSON_GetObjectItem(root, "h_up");
        cJSON *dn = cJSON_GetObjectItem(root, "h_dn");
        
        if (h) current_config.heating_min_hold_ms = h->valueint;
        if (o) current_config.holding_min_hold_ms = o->valueint;
        if (c) current_config.control_interval_ms = c->valueint;
        if (up) current_config.hysteresis_up = up->valuedouble;
        if (dn) current_config.hysteresis_down = dn->valuedouble;
        
        nvs_handle_t my_handle;
        if (nvs_open("gic3500_cfg", NVS_READWRITE, &my_handle) == ESP_OK) {
            nvs_set_blob(my_handle, "advanced", &current_config, sizeof(control_config_t));
            nvs_commit(my_handle);
            nvs_close(my_handle);
            ESP_LOGI(TAG, "Saved advanced configuration to NVS.");
        }
        cJSON_Delete(root);
    }
    
    free(buf);
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t post_ota_handler(httpd_req_t *req) {
    esp_ota_handle_t update_handle = 0;
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    
    if (update_partition == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA partition not found");
        return ESP_FAIL;
    }

    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }

    char *buf = malloc(2048);
    int remaining = req->content_len;
    
    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf, MIN(remaining, 2048));
        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) continue;
            esp_ota_end(update_handle);
            free(buf);
            return ESP_FAIL;
        }

        err = esp_ota_write(update_handle, (const void *)buf, recv_len);
        if (err != ESP_OK) {
            esp_ota_end(update_handle);
            free(buf);
            return ESP_FAIL;
        }
        remaining -= recv_len;
    }
    
    free(buf);
    
    if (esp_ota_end(update_handle) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed");
        return ESP_FAIL;
    }
    
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA set boot failed");
        return ESP_FAIL;
    }
    
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

static esp_err_t post_config_handler(httpd_req_t *req) {
    int total_len = req->content_len;
    if (total_len >= 1000) return ESP_FAIL;
    
    char *buf = malloc(total_len + 1);
    if(httpd_req_recv(req, buf, total_len) <= 0) {
        free(buf); return ESP_FAIL;
    }
    buf[total_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (root) {
        nvs_handle_t my_handle;
        if (nvs_open("gic3500_cfg", NVS_READWRITE, &my_handle) == ESP_OK) {
            cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
            cJSON *pass = cJSON_GetObjectItem(root, "pass");
            cJSON *mqtt = cJSON_GetObjectItem(root, "mqtt");
            cJSON *muser = cJSON_GetObjectItem(root, "muser");
            cJSON *mpass = cJSON_GetObjectItem(root, "mpass");
            
            if (ssid && ssid->valuestring) nvs_set_str(my_handle, "wifi_ssid", ssid->valuestring);
            if (pass && pass->valuestring) nvs_set_str(my_handle, "wifi_pass", pass->valuestring);
            if (mqtt && mqtt->valuestring) nvs_set_str(my_handle, "mqtt_ip", mqtt->valuestring);
            if (muser && muser->valuestring) nvs_set_str(my_handle, "mqtt_user", muser->valuestring);
            if (mpass && mpass->valuestring) nvs_set_str(my_handle, "mqtt_pass", mpass->valuestring);
            
            nvs_commit(my_handle);
            nvs_close(my_handle);
            ESP_LOGI(TAG, "Saved new WiFi/MQTT config to NVS. Rebooting...");
            
            httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        }
        cJSON_Delete(root);
    }
    
    free(buf);
    return ESP_OK;
}

static httpd_uri_t uri_index = { .uri = "/", .method = HTTP_GET, .handler = get_index_handler, .user_ctx = NULL };
static httpd_uri_t uri_start = { .uri = "/api/start", .method = HTTP_POST, .handler = post_start_handler, .user_ctx = NULL };
static httpd_uri_t uri_stop = { .uri = "/api/stop", .method = HTTP_POST, .handler = post_stop_handler, .user_ctx = NULL };
static httpd_uri_t uri_schedule = { .uri = "/api/schedule", .method = HTTP_POST, .handler = post_schedule_handler, .user_ctx = NULL };
static httpd_uri_t uri_schedule_get = { .uri = "/api/schedule", .method = HTTP_GET, .handler = get_schedule_handler, .user_ctx = NULL };
static httpd_uri_t uri_manual = { .uri = "/api/manual", .method = HTTP_POST, .handler = post_manual_handler, .user_ctx = NULL };
static httpd_uri_t uri_log = { .uri = "/api/log", .method = HTTP_POST, .handler = post_log_handler, .user_ctx = NULL };
static httpd_uri_t uri_status = { .uri = "/api/status", .method = HTTP_GET, .handler = get_status_handler, .user_ctx = NULL };
static httpd_uri_t uri_config = { .uri = "/api/config", .method = HTTP_POST, .handler = post_config_handler, .user_ctx = NULL };
static httpd_uri_t uri_ota = { .uri = "/api/ota", .method = HTTP_POST, .handler = post_ota_handler, .user_ctx = NULL };
static httpd_uri_t uri_advanced = { .uri = "/api/advanced", .method = HTTP_POST, .handler = post_advanced_handler, .user_ctx = NULL };

void web_server_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 13;

    current_schedule.num_steps = 0;
    nvs_handle_t my_handle;
    if (nvs_open("gic3500_cfg", NVS_READONLY, &my_handle) == ESP_OK) {
        size_t required_size = sizeof(mash_schedule_t);
        if (nvs_get_blob(my_handle, "schedule", &current_schedule, &required_size) == ESP_OK) {
            ESP_LOGI(TAG, "Loaded schedule from NVS: %d steps", current_schedule.num_steps);
        }
        
        size_t cfg_size = sizeof(control_config_t);
        if (nvs_get_blob(my_handle, "advanced", &current_config, &cfg_size) == ESP_OK) {
            ESP_LOGI(TAG, "Loaded advanced config from NVS");
        }
        
        nvs_close(my_handle);
    }

    ESP_LOGI(TAG, "Starting web server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &uri_index);
        httpd_register_uri_handler(server, &uri_start);
        httpd_register_uri_handler(server, &uri_stop);
        httpd_register_uri_handler(server, &uri_schedule);
        httpd_register_uri_handler(server, &uri_schedule_get);
        httpd_register_uri_handler(server, &uri_manual);
        httpd_register_uri_handler(server, &uri_log);
        httpd_register_uri_handler(server, &uri_status);
        httpd_register_uri_handler(server, &uri_config);
        httpd_register_uri_handler(server, &uri_ota);
        httpd_register_uri_handler(server, &uri_advanced);
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

int get_manual_stage(void) {
    return current_manual_stage;
}

void set_manual_stage(int stage) {
    current_manual_stage = stage;
}
