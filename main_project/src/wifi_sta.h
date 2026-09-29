/*
 * Wi-Fi STA connect, same event-group pattern as miniproject_module3's
 * subwoofer amp (WIFI_EVENT_STA_START -> connect, STA_DISCONNECTED ->
 * reconnect, IP_EVENT_STA_GOT_IP -> unblock). Reports connection state into
 * sensor_hub so the dashboard can show it.
 */
#pragma once

#include <stdbool.h>

// Starts Wi-Fi STA and waits up to WIFI_CONNECT_TIMEOUT_MS for the initial
// connection. Returns true if connected within that window, false if it
// timed out (bad credentials, AP out of range, etc.) — the caller should
// keep booting the rest of the system either way. A background reconnect
// keeps retrying on disconnect, so sensor_hub_set_wifi()/the dashboard will
// pick it up later if the connection succeeds after this call returns.
bool wifi_sta_init(void);
