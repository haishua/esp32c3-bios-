#ifndef SPI_FLASH_H
#define SPI_FLASH_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <esp_err.h>

#define SPI_FLASH_SECTOR_SIZE 4096
#define SPI_FLASH_PAGE_SIZE 256
#define SPI_FLASH_BLOCK_SIZE 65536

esp_err_t spi_flash_init(void);
esp_err_t spi_flash_read_id(uint8_t *manufacturer_id, uint8_t *device_id);
esp_err_t spi_flash_read_id_full(uint8_t *manufacturer_id, uint8_t *device_id, uint8_t *capacity_id);
uint32_t spi_flash_get_size(uint8_t manufacturer_id, uint8_t device_id, uint8_t capacity_id);
esp_err_t spi_flash_read_status(uint8_t *status);
esp_err_t spi_flash_write_enable(void);
esp_err_t spi_flash_erase_sector(uint32_t sector_addr);
esp_err_t spi_flash_erase_chip(void);
esp_err_t spi_flash_write_page(uint32_t addr, const uint8_t *data, size_t len);
esp_err_t spi_flash_read(uint32_t addr, uint8_t *data, size_t len);
esp_err_t spi_flash_wait_busy(void);
esp_err_t spi_flash_unlock(void);

#endif // SPI_FLASH_H