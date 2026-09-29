/*
 * Wi-Fi STA connect, same event-group pattern as miniproject_module3's
 * subwoofer amp (WIFI_EVENT_STA_START -> connect, STA_DISCONNECTED ->
 * reconnect, IP_EVENT_STA_GOT_IP -> unblock). Reports connection state into
 * sensor_hub so the dashboard can show it.
 */
#pragma once

// Blocks until connected (or forever, if the credentials are wrong — same
// known limitation as mini3's UDP audio project).
void wifi_sta_init(void);
