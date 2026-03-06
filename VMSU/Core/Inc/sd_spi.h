#ifndef SD_SPI_H
#define SD_SPI_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

typedef enum {
  SD_OK = 0,
  SD_ERR,
  SD_TIMEOUT,
  SD_NO_INIT
} sd_status_t;

/**
 * Initialize SD card in SPI mode.
 * Returns SD_OK on success.
 */
sd_status_t sd_spi_init(void);

/**
 * Read `count` 512-byte sectors starting at `sector` into `buff`.
 * buff size must be 512*count.
 */
sd_status_t sd_spi_read_sectors(uint8_t *buff, uint32_t sector, uint32_t count);

/**
 * Write `count` 512-byte sectors starting at `sector` from `buff`.
 */
sd_status_t sd_spi_write_sectors(const uint8_t *buff, uint32_t sector, uint32_t count);

/**
 * Get card type flags (bitmask): 1=SDv2, 2=SDHC/SDXC(block addressing)
 */
uint8_t sd_spi_get_type(void);

#endif
