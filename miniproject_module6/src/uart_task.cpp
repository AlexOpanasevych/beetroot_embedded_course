/*
 * UART Task — reads command lines from the console (the same USB-serial
 * link `pio device monitor` uses) and forwards parsed commands to the
 * Manager task via xUartCmdQueue. Runs on stdin/getchar() rather than a
 * second uart_driver_install(), so it shares the existing ESP-IDF console
 * UART instead of fighting it for the port.
 */

#include "common.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static uart_cmd_t parse_command(char *line)
{
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' || line[len - 1] == ' ')) {
        line[--len] = '\0';
    }
    for (size_t i = 0; i < len; i++) {
        line[i] = (char)tolower((unsigned char)line[i]);
    }

    if (len == 0) return CMD_UNKNOWN;
    if (strcmp(line, "status") == 0) return CMD_STATUS;
    if (strcmp(line, "log on") == 0) return CMD_LOG_ON;
    if (strcmp(line, "log off") == 0) return CMD_LOG_OFF;
    if (strcmp(line, "flush") == 0) return CMD_FLUSH;
    if (strcmp(line, "erase") == 0) return CMD_ERASE;
    return CMD_UNKNOWN;
}

void uart_task(void *pv)
{
    char line[UART_CMD_LINE_MAX];
    size_t pos = 0;

    printf("\r\n[UART] Ready. Commands: status | log on | log off | flush | erase\r\n");

    while (1) {
        int c = getchar();
        if (c == EOF) {
            // No byte available right now (console read timed out) -
            // yield instead of busy-polling.
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (c == '\r' || c == '\n') {
            if (pos == 0) continue;
            line[pos] = '\0';
            pos = 0;

            uart_cmd_t cmd = parse_command(line);
            if (cmd == CMD_UNKNOWN) {
                printf("\r\n[UART] Unknown command.\r\n");
            } else {
                xQueueSend(xUartCmdQueue, &cmd, portMAX_DELAY);
            }
        } else if (pos < sizeof(line) - 1) {
            line[pos++] = (char)c;
        }
    }
}
