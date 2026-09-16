#include "common.h"
#include "display.h"

// Blocks on the latest display frame from the Manager task and renders it
// under xI2CMutex, since the SSD1306 shares the I2C bus with the sensors.
void display_task(void *pv)
{
    display_msg_t msg;
    while (1) {
        if (xQueueReceive(xDisplayQueue, &msg, portMAX_DELAY) == pdTRUE) {
            if (xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                display_render(&msg);
                xSemaphoreGive(xI2CMutex);
            }
        }
    }
}
