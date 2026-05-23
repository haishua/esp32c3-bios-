#ifndef BIOS_FLASHER_H
#define BIOS_FLASHER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MAX_BIOS_SIZE (16 * 1024 * 1024)

typedef enum {
    BIOS_FLASH_SUCCESS,
    BIOS_FLASH_ERR_INIT,
    BIOS_FLASH_ERR_READ_ID,
    BIOS_FLASH_ERR_ERASE,
    BIOS_FLASH_ERR_WRITE,
    BIOS_FLASH_ERR_VERIFY,
    BIOS_FLASH_ERR_INVALID_SIZE,
} bios_flash_result_t;

bios_flash_result_t bios_flasher_init(void);
bios_flash_result_t bios_flasher_read_id(uint8_t *manufacturer_id, uint8_t *device_id);
bios_flash_result_t bios_flasher_read_id_full(uint8_t *manufacturer_id, uint8_t *device_id, uint8_t *capacity_id);
bios_flash_result_t bios_flasher_backup(const char *filename, size_t size);
bios_flash_result_t bios_flasher_write(const uint8_t *data, size_t size);
bios_flash_result_t bios_flasher_verify(const uint8_t *data, size_t size);
void bios_flasher_print_progress(uint32_t current, uint32_t total);

#endif // BIOS_FLASHER_H