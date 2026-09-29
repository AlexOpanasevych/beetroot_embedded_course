#include "crit_timing.h"

#include <string.h>
#include "esp_attr.h"
#include "esp_log.h"

void crit_timing_reset(crit_timing_stats_t *stats)
{
    memset(stats, 0, sizeof(*stats));
    stats->min_us = UINT32_MAX;
}

void IRAM_ATTR crit_timing_record(crit_timing_stats_t *stats, uint32_t elapsed_us)
{
    stats->count++;
    stats->sum_us += elapsed_us;
    if (elapsed_us < stats->min_us) stats->min_us = elapsed_us;
    if (elapsed_us > stats->max_us) stats->max_us = elapsed_us;
}

void crit_timing_log(const char *tag, const char *label, const crit_timing_stats_t *stats)
{
    if (stats->count == 0) {
        ESP_LOGI(tag, "%s: no samples this window", label);
        return;
    }

    uint32_t avg_us = (uint32_t)(stats->sum_us / stats->count);
    ESP_LOGI(tag, "%s: n=%lu min=%luus max=%luus avg=%luus",
             label, (unsigned long)stats->count, (unsigned long)stats->min_us,
             (unsigned long)stats->max_us, (unsigned long)avg_us);
}
