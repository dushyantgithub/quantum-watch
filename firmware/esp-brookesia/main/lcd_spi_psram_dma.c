/*
 * SPDX-FileCopyrightText: 2026 Dushyant Singh
 * SPDX-License-Identifier: MIT
 */

#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_memory_utils.h"

static const char *TAG = "VokrrLCD";

esp_err_t __real_spi_device_queue_trans(spi_device_handle_t handle,
                                        spi_transaction_t *trans_desc,
                                        TickType_t ticks_to_wait);

/* ESP-IDF's LCD SPI transport doesn't opt external color buffers into direct
 * PSRAM DMA. Its default fallback allocates one internal bounce buffer for
 * every queued QSPI chunk, exhausting internal RAM after Wi-Fi and BLE start.
 * Restrict the override to large QSPI writes so unrelated SPI traffic keeps
 * the framework's default behavior. */
esp_err_t __wrap_spi_device_queue_trans(spi_device_handle_t handle,
                                        spi_transaction_t *trans_desc,
                                        TickType_t ticks_to_wait)
{
    static bool direct_dma_reported = false;
    if (trans_desc && trans_desc->tx_buffer &&
            trans_desc->length >= (4096 * 8) &&
            (trans_desc->flags & SPI_TRANS_MODE_QIO) &&
            esp_ptr_external_ram(trans_desc->tx_buffer)) {
        trans_desc->flags |= SPI_TRANS_DMA_USE_PSRAM;
        if (!direct_dma_reported) {
            direct_dma_reported = true;
            ESP_LOGI(TAG, "Direct PSRAM DMA enabled for QSPI display flushes");
        }
    }
    return __real_spi_device_queue_trans(handle, trans_desc, ticks_to_wait);
}
