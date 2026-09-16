#include "flash_log.h"
#include "common.h"
#include <stdio.h>
#include <string.h>
#include "esp_partition.h"
#include "esp_log.h"

static const char *TAG = "FLASHLOG";

static const esp_partition_t *s_partition = nullptr;
static uint8_t   s_sector_buf[FLASH_SECTOR_SIZE];
static size_t    s_buf_used = 0;      // bytes filled in s_sector_buf so far
static size_t    s_write_offset = 0;  // partition offset of the next sector to commit
static volatile uint32_t s_entries_written = 0;
static volatile bool     s_full = false;

static void reset_buffer(void)
{
    memset(s_sector_buf, 0xFF, sizeof(s_sector_buf));
    s_buf_used = 0;
}

esp_err_t flashlog_init(void)
{
    s_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                            (esp_partition_subtype_t)0x40,
                                            LOGDATA_PARTITION_NAME);
    if (s_partition == nullptr) {
        ESP_LOGE(TAG, "partition \"%s\" not found - check partitions.csv", LOGDATA_PARTITION_NAME);
        return ESP_ERR_NOT_FOUND;
    }
    if (s_partition->size % FLASH_SECTOR_SIZE != 0) {
        ESP_LOGE(TAG, "partition size (%lu) is not sector-aligned", (unsigned long)s_partition->size);
        return ESP_ERR_INVALID_SIZE;
    }

    s_write_offset = 0;
    s_entries_written = 0;
    s_full = false;
    reset_buffer();

    ESP_LOGI(TAG, "\"%s\" ready: %lu bytes, %lu entries/sector",
             LOGDATA_PARTITION_NAME, (unsigned long)s_partition->size,
             (unsigned long)(FLASH_SECTOR_SIZE / sizeof(log_entry_t)));
    return ESP_OK;
}

// Erases+writes s_sector_buf (whatever it currently holds, zero-padded with
// 0xFF past s_buf_used) to s_write_offset, then advances to the next
// sector. Used both when the buffer fills up naturally and for "flush".
static esp_err_t commit_sector(void)
{
    if (s_write_offset + FLASH_SECTOR_SIZE > s_partition->size) {
        s_full = true;
        printf("\r\n[FLASH] Partition full! Logging stopped.\r\n");
        ESP_LOGW(TAG, "partition full at offset %lu", (unsigned long)s_write_offset);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_partition_erase_range(s_partition, s_write_offset, FLASH_SECTOR_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "erase failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_partition_write(s_partition, s_write_offset, s_sector_buf, FLASH_SECTOR_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "write failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "committed sector at offset %lu", (unsigned long)s_write_offset);
    s_write_offset += FLASH_SECTOR_SIZE;
    reset_buffer();

    if (s_write_offset + FLASH_SECTOR_SIZE > s_partition->size) {
        s_full = true;
    }
    return ESP_OK;
}

static void do_append(const log_entry_t *entry)
{
    if (s_full) return;

    memcpy(&s_sector_buf[s_buf_used], entry, sizeof(*entry));
    s_buf_used += sizeof(*entry);
    s_entries_written = s_entries_written + 1;

    if (s_buf_used + sizeof(*entry) > FLASH_SECTOR_SIZE) {
        commit_sector();
    }
}

static void do_flush(void)
{
    if (s_buf_used == 0) {
        printf("\r\n[FLASH] Nothing to flush.\r\n");
        return;
    }
    if (commit_sector() == ESP_OK) {
        printf("\r\n[FLASH] Buffer flushed to flash.\r\n");
    }
}

static void do_erase(void)
{
    esp_err_t err = esp_partition_erase_range(s_partition, 0, s_partition->size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "erase failed: %s", esp_err_to_name(err));
        printf("\r\n[FLASH] Erase failed: %s\r\n", esp_err_to_name(err));
        return;
    }
    s_write_offset = 0;
    s_entries_written = 0;
    s_full = false;
    reset_buffer();
    printf("\r\n[FLASH] Partition erased. Logging can resume.\r\n");
}

bool flashlog_is_full(void) { return s_full; }
uint32_t flashlog_entries_written(void) { return s_entries_written; }

uint8_t flashlog_usage_pct(void)
{
    if (s_partition == nullptr || s_partition->size == 0) return 0;
    size_t used = s_write_offset + s_buf_used;
    uint32_t pct = (uint32_t)((uint64_t)used * 100 / s_partition->size);
    return (uint8_t)(pct > 100 ? 100 : pct);
}

void flash_writer_task(void *pv)
{
    flash_msg_t msg;
    while (1) {
        if (xQueueReceive(xFlashQueue, &msg, portMAX_DELAY) == pdTRUE) {
            switch (msg.op) {
                case FLASH_OP_APPEND: do_append(&msg.entry); break;
                case FLASH_OP_FLUSH:  do_flush(); break;
                case FLASH_OP_ERASE:  do_erase(); break;
            }
        }
    }
}
