#include "bios_flasher.h"
#include "spi_flash.h"
#include "esp_log.h"

static const char *TAG = "bios_flasher";

bios_flash_result_t bios_flasher_init(void) {
    esp_err_t ret = spi_flash_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI flash init failed: %s", esp_err_to_name(ret));
        return BIOS_FLASH_ERR_INIT;
    }
    ESP_LOGI(TAG, "SPI flash initialized successfully");
    return BIOS_FLASH_SUCCESS;
}

bios_flash_result_t bios_flasher_read_id(uint8_t *manufacturer_id, uint8_t *device_id) {
    esp_err_t ret = spi_flash_read_id(manufacturer_id, device_id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Read ID failed: %s", esp_err_to_name(ret));
        return BIOS_FLASH_ERR_READ_ID;
    }
    ESP_LOGI(TAG, "Flash ID: Manufacturer=0x%02X, Device=0x%02X", *manufacturer_id, *device_id);
    return BIOS_FLASH_SUCCESS;
}

bios_flash_result_t bios_flasher_read_id_full(uint8_t *manufacturer_id, uint8_t *device_id, uint8_t *capacity_id) {
    esp_err_t ret = spi_flash_read_id_full(manufacturer_id, device_id, capacity_id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Read ID failed: %s", esp_err_to_name(ret));
        return BIOS_FLASH_ERR_READ_ID;
    }
    ESP_LOGI(TAG, "Flash ID: Manufacturer=0x%02X, Device=0x%02X, Capacity=0x%02X",
             *manufacturer_id, *device_id, *capacity_id);
    return BIOS_FLASH_SUCCESS;
}

void bios_flasher_print_progress(uint32_t current, uint32_t total) {
    static uint32_t last_percent = 0;
    uint32_t percent = (current * 100) / total;
    
    if (percent != last_percent && percent % 10 == 0) {
        ESP_LOGI(TAG, "Progress: %u%% (%u/%u)", percent, current, total);
        last_percent = percent;
    }
}

bios_flash_result_t bios_flasher_write(const uint8_t *data, size_t size) {
    if (size > MAX_BIOS_SIZE) {
        ESP_LOGE(TAG, "BIOS size too large: %u > %u", size, MAX_BIOS_SIZE);
        return BIOS_FLASH_ERR_INVALID_SIZE;
    }

    ESP_LOGI(TAG, "Starting erase (size: %u bytes)", size);
    
    for (size_t addr = 0; addr < size; addr += SPI_FLASH_SECTOR_SIZE) {
        esp_err_t ret = spi_flash_erase_sector(addr);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Erase failed at sector 0x%08X: %s", addr, esp_err_to_name(ret));
            return BIOS_FLASH_ERR_ERASE;
        }
        bios_flasher_print_progress(addr + SPI_FLASH_SECTOR_SIZE, size);
    }

    ESP_LOGI(TAG, "Erase completed, starting write");

    for (size_t addr = 0; addr < size; addr += SPI_FLASH_PAGE_SIZE) {
        size_t write_size = (size - addr) > SPI_FLASH_PAGE_SIZE ? SPI_FLASH_PAGE_SIZE : (size - addr);
        
        esp_err_t ret = spi_flash_write_page(addr, &data[addr], write_size);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Write failed at address 0x%08X: %s", addr, esp_err_to_name(ret));
            return BIOS_FLASH_ERR_WRITE;
        }

        bios_flasher_print_progress(addr + write_size, size);
    }

    ESP_LOGI(TAG, "Write completed successfully");
    return BIOS_FLASH_SUCCESS;
}

bios_flash_result_t bios_flasher_verify(const uint8_t *data, size_t size) {
    if (size > MAX_BIOS_SIZE) {
        ESP_LOGE(TAG, "BIOS size too large: %u > %u", size, MAX_BIOS_SIZE);
        return BIOS_FLASH_ERR_INVALID_SIZE;
    }

    ESP_LOGI(TAG, "Starting verification (size: %u bytes)", size);
    
    uint8_t *buffer = malloc(SPI_FLASH_PAGE_SIZE);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate memory for buffer");
        return BIOS_FLASH_ERR_VERIFY;
    }

    for (size_t addr = 0; addr < size; addr += SPI_FLASH_PAGE_SIZE) {
        size_t read_size = (size - addr) > SPI_FLASH_PAGE_SIZE ? SPI_FLASH_PAGE_SIZE : (size - addr);
        
        esp_err_t ret = spi_flash_read(addr, buffer, read_size);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Read failed at address 0x%08X: %s", addr, esp_err_to_name(ret));
            free(buffer);
            return BIOS_FLASH_ERR_VERIFY;
        }

        if (memcmp(buffer, &data[addr], read_size) != 0) {
            ESP_LOGE(TAG, "Verification failed at address 0x%08X", addr);
            free(buffer);
            return BIOS_FLASH_ERR_VERIFY;
        }

        bios_flasher_print_progress(addr + read_size, size);
    }

    free(buffer);
    ESP_LOGI(TAG, "Verification completed successfully");
    return BIOS_FLASH_SUCCESS;
}
