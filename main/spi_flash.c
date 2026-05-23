#include "spi_flash.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include <string.h>
#include <malloc.h>

#define SPI_HOST SPI2_HOST
#define PIN_NUM_MISO 8
#define PIN_NUM_MOSI 12
#define PIN_NUM_CLK 10
#define PIN_NUM_CS 5

static const char *TAG = "spi_flash";
static spi_device_handle_t spi_handle = NULL;

esp_err_t spi_flash_init(void) {
    esp_err_t ret;

    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 8192,
    };

    ret = spi_bus_initialize(SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 1 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = PIN_NUM_CS,
        .queue_size = 7,
        .pre_cb = NULL,
    };

    ret = spi_bus_add_device(SPI_HOST, &devcfg, &spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(ret));
    }
    ESP_LOGI(TAG, "SPI flash init done, mode=0, clk=1MHz");
    return ret;
}

esp_err_t spi_flash_read_id(uint8_t *manufacturer_id, uint8_t *device_id) {
    esp_err_t ret;
    uint8_t tx_data[4] = {0x9F, 0x00, 0x00, 0x00};
    uint8_t rx_data[4] = {0};

    spi_transaction_t t = {
        .length = 32,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data,
    };

    ret = spi_device_transmit(spi_handle, &t);
    if (ret == ESP_OK) {
        *manufacturer_id = rx_data[1];
        *device_id = rx_data[2];
        ESP_LOGI(TAG, "Raw ID bytes: 0x%02X 0x%02X 0x%02X",
                 rx_data[1], rx_data[2], rx_data[3]);
    } else {
        ESP_LOGE(TAG, "Read ID transmit failed: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t spi_flash_read_id_full(uint8_t *manufacturer_id, uint8_t *device_id, uint8_t *capacity_id) {
    esp_err_t ret;
    uint8_t tx_data[4] = {0x9F, 0x00, 0x00, 0x00};
    uint8_t rx_data[4] = {0};

    spi_transaction_t t = {
        .length = 32,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data,
    };

    ret = spi_device_transmit(spi_handle, &t);
    if (ret == ESP_OK) {
        *manufacturer_id = rx_data[1];
        *device_id = rx_data[2];
        *capacity_id = rx_data[3];
        ESP_LOGI(TAG, "JEDEC ID: 0x%02X 0x%02X 0x%02X",
                 rx_data[1], rx_data[2], rx_data[3]);
    } else {
        ESP_LOGE(TAG, "Read ID transmit failed: %s", esp_err_to_name(ret));
    }

    return ret;
}

uint32_t spi_flash_get_size(uint8_t manufacturer_id, uint8_t device_id, uint8_t capacity_id) {
    if (manufacturer_id == 0xEF) {
        switch (capacity_id) {
            case 0x14: return 1 * 1024 * 1024;
            case 0x15: return 2 * 1024 * 1024;
            case 0x16: return 4 * 1024 * 1024;
            case 0x17: return 4 * 1024 * 1024;
            case 0x18: return 8 * 1024 * 1024;
            case 0x19: return 16 * 1024 * 1024;
            default: return 4 * 1024 * 1024;
        }
    } else if (manufacturer_id == 0x68) {
        switch (capacity_id) {
            case 0x16: return 4 * 1024 * 1024;
            case 0x17: return 4 * 1024 * 1024;
            case 0x18: return 8 * 1024 * 1024;
            default: return 4 * 1024 * 1024;
        }
    } else if (manufacturer_id == 0xC2) {
        switch (capacity_id) {
            case 0x15: return 2 * 1024 * 1024;
            case 0x16: return 4 * 1024 * 1024;
            case 0x17: return 8 * 1024 * 1024;
            case 0x18: return 16 * 1024 * 1024;
            default: return 8 * 1024 * 1024;
        }
    }
    return 4 * 1024 * 1024;
}

esp_err_t spi_flash_read_status(uint8_t *status) {
    esp_err_t ret;
    uint8_t tx_data[2] = {0x05, 0x00};
    uint8_t rx_data[2] = {0};

    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data,
    };

    ret = spi_device_transmit(spi_handle, &t);
    if (ret == ESP_OK) {
        *status = rx_data[1];
    }

    return ret;
}

esp_err_t spi_flash_write_enable(void) {
    esp_err_t ret;
    uint8_t cmd = 0x06;

    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
        .rx_buffer = NULL,
    };

    ret = spi_device_transmit(spi_handle, &t);
    return ret;
}

esp_err_t spi_flash_wait_busy(void) {
    esp_err_t ret;
    uint8_t status;

    do {
        ret = spi_flash_read_status(&status);
        if (ret != ESP_OK) return ret;
    } while (status & 0x01);

    return ESP_OK;
}

esp_err_t spi_flash_erase_sector(uint32_t sector_addr) {
    esp_err_t ret;
    uint8_t data[4] = {0x20, (sector_addr >> 16) & 0xFF, (sector_addr >> 8) & 0xFF, sector_addr & 0xFF};

    ret = spi_flash_write_enable();
    if (ret != ESP_OK) return ret;

    spi_transaction_t t = {
        .length = 32,
        .tx_buffer = data,
        .rx_buffer = NULL,
    };

    ret = spi_device_transmit(spi_handle, &t);
    if (ret != ESP_OK) return ret;

    return spi_flash_wait_busy();
}

esp_err_t spi_flash_erase_chip(void) {
    esp_err_t ret;
    uint8_t cmd = 0xC7;

    ret = spi_flash_write_enable();
    if (ret != ESP_OK) return ret;

    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
        .rx_buffer = NULL,
    };

    ret = spi_device_transmit(spi_handle, &t);
    if (ret != ESP_OK) return ret;

    return spi_flash_wait_busy();
}

esp_err_t spi_flash_unlock(void) {
    esp_err_t ret;
    
    uint8_t status;
    ret = spi_flash_read_status(&status);
    if (ret != ESP_OK) return ret;
    
    ESP_LOGI(TAG, "Current status: 0x%02X", status);
    
    if (status & 0x1C) {
        ESP_LOGI(TAG, "Clearing block protect bits");
        
        ret = spi_flash_write_enable();
        if (ret != ESP_OK) return ret;
        
        uint8_t tx_data[2] = {0x01, 0x00};
        spi_transaction_t t = {
            .length = 16,
            .tx_buffer = tx_data,
            .rx_buffer = NULL,
        };
        ret = spi_device_transmit(spi_handle, &t);
        if (ret != ESP_OK) return ret;
        
        ret = spi_flash_wait_busy();
        
        spi_flash_read_status(&status);
        ESP_LOGI(TAG, "After unlock: status=0x%02X", status);
    }
    
    return ESP_OK;
}

esp_err_t spi_flash_write_page(uint32_t addr, const uint8_t *data, size_t len) {
    esp_err_t ret;
    if (len > SPI_FLASH_PAGE_SIZE) return ESP_ERR_INVALID_ARG;

    static uint8_t tx_data[260] __attribute__((aligned(4)));

    tx_data[0] = 0x02;
    tx_data[1] = (addr >> 16) & 0xFF;
    tx_data[2] = (addr >> 8) & 0xFF;
    tx_data[3] = addr & 0xFF;
    memcpy(&tx_data[4], data, len);

    uint8_t status_before;
    spi_flash_read_status(&status_before);
    ESP_LOGI(TAG, "Before WREN: status=0x%02X", status_before);

    ret = spi_flash_write_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Write enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    uint8_t status_after;
    spi_flash_read_status(&status_after);
    ESP_LOGI(TAG, "After WREN: status=0x%02X, WEL=%d", status_after, (status_after >> 1) & 1);
    
    if (!(status_after & 0x02)) {
        ESP_LOGE(TAG, "WEL bit not set! Write will fail.");
        return ESP_FAIL;
    }

    spi_transaction_t t = {
        .length = (len + 4) * 8,
        .tx_buffer = tx_data,
        .rx_buffer = NULL,
    };

    ret = spi_device_transmit(spi_handle, &t);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Transmit failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = spi_flash_wait_busy();
    
    uint8_t status_final;
    spi_flash_read_status(&status_final);
    ESP_LOGI(TAG, "After write: status=0x%02X", status_final);
    
    return ret;
}

esp_err_t spi_flash_read(uint32_t addr, uint8_t *data, size_t len) {
    if (len == 0) return ESP_OK;
    if (data == NULL) return ESP_ERR_INVALID_ARG;

    esp_err_t ret = ESP_OK;
    const size_t chunk_size = 32;
    const size_t buf_size = chunk_size + 4;

    static uint8_t tx_buf[64] __attribute__((aligned(4)));
    static uint8_t rx_buf[64] __attribute__((aligned(4)));

    size_t offset = 0;
    while (offset < len) {
        size_t remaining = len - offset;
        size_t read_len = (remaining > chunk_size) ? chunk_size : remaining;

        tx_buf[0] = 0x03;
        tx_buf[1] = ((addr + offset) >> 16) & 0xFF;
        tx_buf[2] = ((addr + offset) >> 8) & 0xFF;
        tx_buf[3] = (addr + offset) & 0xFF;
        memset(&tx_buf[4], 0, read_len);

        spi_transaction_t t = {
            .length = (read_len + 4) * 8,
            .tx_buffer = tx_buf,
            .rx_buffer = rx_buf,
        };

        ret = spi_device_transmit(spi_handle, &t);

        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Read transmit failed at offset %u: %s", offset, esp_err_to_name(ret));
            break;
        }

        memcpy(data + offset, &rx_buf[4], read_len);
        offset += read_len;
    }

    return ret;
}