/*
 * Raw log storage on the "logdata" partition.
 *
 * Design, matching the assignment exactly:
 *   - one RAM buffer sized to exactly one flash sector (FLASH_SECTOR_SIZE);
 *   - the sector is only erased+written to flash once that buffer is full,
 *     or on an explicit "flush";
 *   - no wear leveling, no metadata stored in flash — just back-to-back
 *     log_entry_t records, sector by sector, from the start of the
 *     partition;
 *   - once the partition is full, logging stops until "erase".
 *
 * All partition access happens on the Flash Writer task (flash_writer_task,
 * the consumer of xFlashQueue) so appends/flush/erase can never race each
 * other.
 */

#pragma once

#include "esp_err.h"

esp_err_t flashlog_init(void);

bool     flashlog_is_full(void);
uint32_t flashlog_entries_written(void);
uint8_t  flashlog_usage_pct(void); // 0-100, across the whole partition
