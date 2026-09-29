/*
 * Minimal esp_http_server dashboard: "/" serves a self-contained HTML page
 * (no external assets — the ESP32 has no general internet access guarantee),
 * "/status.json" serves the current sensor_hub snapshot for that page's
 * polling fetch(). Call after Wi-Fi is connected.
 */
#pragma once

void web_dashboard_start(void);
