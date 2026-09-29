#include "web_dashboard.h"
#include "sensor_hub.h"

#include <stdio.h>
#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "WEB_DASHBOARD";

static const char *INDEX_HTML =
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Desk Guardian</title>"
    "<style>"
    "body{font-family:system-ui,sans-serif;background:#111;color:#eee;margin:0;padding:24px}"
    "h1{font-size:1.3rem;margin:0 0 16px}"
    ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:12px;max-width:640px}"
    ".card{background:#1c1c1c;border-radius:10px;padding:14px}"
    ".label{font-size:.75rem;color:#999;text-transform:uppercase;letter-spacing:.04em}"
    ".value{font-size:1.6rem;margin-top:4px}"
    ".alert{background:#5a1a1a}"
    ".ok{color:#7fd17f}.bad{color:#e07a7a}"
    "</style></head><body>"
    "<h1>Desk Guardian &mdash; Networked Sentry</h1>"
    "<div class='grid' id='grid'>"
    "<div class='card'><div class='label'>Temperature</div><div class='value' id='temp'>--</div></div>"
    "<div class='card'><div class='label'>Humidity</div><div class='value' id='hum'>--</div></div>"
    "<div class='card'><div class='label'>Pressure</div><div class='value' id='press'>--</div></div>"
    "<div class='card'><div class='label'>Fan duty</div><div class='value' id='fan'>--</div></div>"
    "<div class='card' id='motion_card'><div class='label'>Motion</div><div class='value' id='motion'>--</div></div>"
    "<div class='card'><div class='label'>Motion events</div><div class='value' id='count'>--</div></div>"
    "</div>"
    "<script>"
    "async function poll(){"
    "try{"
    "const r=await fetch('/status.json');const s=await r.json();"
    "document.getElementById('temp').textContent=s.bme280_ok?s.temperature_c.toFixed(1)+' \\u00b0C':'n/a';"
    "document.getElementById('hum').textContent=s.bme280_ok?s.humidity_pct.toFixed(0)+' %':'n/a';"
    "document.getElementById('press').textContent=s.bme280_ok?s.pressure_hpa.toFixed(0)+' hPa':'n/a';"
    "document.getElementById('fan').textContent=Math.round(100*s.fan_duty/s.fan_duty_max)+' %';"
    "document.getElementById('motion').textContent=s.motion_active?'ALERT':'clear';"
    "document.getElementById('motion_card').className='card'+(s.motion_active?' alert':'');"
    "document.getElementById('count').textContent=s.motion_count;"
    "}catch(e){}"
    "}"
    "poll();setInterval(poll,2000);"
    "</script></body></html>";

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status_handler(httpd_req_t *req)
{
    system_state_t s;
    sensor_hub_get_state(&s);

    char buf[384];
    snprintf(buf, sizeof(buf),
             "{"
             "\"bme280_ok\":%s,\"temperature_c\":%.2f,\"humidity_pct\":%.2f,\"pressure_hpa\":%.2f,"
             "\"motion_active\":%s,\"motion_count\":%lu,\"last_motion_ms\":%lu,"
             "\"fan_duty\":%lu,\"fan_duty_max\":%lu,"
             "\"wifi_connected\":%s}",
             s.bme280_ok ? "true" : "false", s.temperature_c, s.humidity_pct, s.pressure_hpa,
             s.motion_active ? "true" : "false", (unsigned long)s.motion_count, (unsigned long)s.last_motion_ms,
             (unsigned long)s.fan_duty, (unsigned long)s.fan_duty_max, s.wifi_connected ? "true" : "false");

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
}

void web_dashboard_start(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    httpd_uri_t index_uri = {.uri = "/", .method = HTTP_GET, .handler = index_handler, .user_ctx = NULL};
    httpd_uri_t status_uri = {.uri = "/status.json", .method = HTTP_GET, .handler = status_handler, .user_ctx = NULL};

    httpd_register_uri_handler(server, &index_uri);
    httpd_register_uri_handler(server, &status_uri);

    ESP_LOGI(TAG, "Dashboard started");
}
