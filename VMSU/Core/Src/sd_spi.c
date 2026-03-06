#include "sd_spi.h"
#include <string.h>

/* -------- USER CONFIG (EDIT IF NEEDED) -------- */
extern SPI_HandleTypeDef hspi2;        // from spi.c

#define SD_CS_GPIO_Port GPIOB
#define SD_CS_Pin       GPIO_PIN_12

/* Timeout tuning */
#define SD_INIT_TIMEOUT_MS   2000
#define SD_RW_TIMEOUT_MS     2000
#define SD_TOKEN_TIMEOUT_MS  300

/* SPI helpers */
static inline void SD_CS_LOW(void)  { HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_RESET); }
static inline void SD_CS_HIGH(void) { HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);   }

static uint8_t sd_type = 0; // bit0=SDv2, bit1=SDHC(block addressing)

static uint8_t spi_txrx(uint8_t data)
{
  uint8_t rx = 0xFF;
  HAL_SPI_TransmitReceive(&hspi2, &data, &rx, 1, HAL_MAX_DELAY);
  return rx;
}

static void spi_clock_dummy(uint32_t nbytes)
{
  while (nbytes--) spi_txrx(0xFF);
}

/* Wait for card ready (MISO=1 / returns 0xFF) */
static uint8_t wait_ready(uint32_t timeout_ms)
{
  uint32_t start = HAL_GetTick();
  uint8_t r;
  do {
    r = spi_txrx(0xFF);
    if (r == 0xFF) return 1;
  } while ((HAL_GetTick() - start) < timeout_ms);
  return 0;
}

/* Receive a data block (token 0xFE) */
static uint8_t recv_data_block(uint8_t *buff, uint32_t btr)
{
  uint32_t start = HAL_GetTick();
  uint8_t token;

  do {
    token = spi_txrx(0xFF);
    if (token == 0xFE) break;
  } while ((HAL_GetTick() - start) < SD_TOKEN_TIMEOUT_MS);

  if (token != 0xFE) return 0;

  // Read data
  for (uint32_t i = 0; i < btr; i++) buff[i] = spi_txrx(0xFF);

  // Discard CRC
  spi_txrx(0xFF);
  spi_txrx(0xFF);

  return 1;
}

/* Transmit a data block */
/* Transmit a data block */
static uint8_t xmit_data_block(const uint8_t *buff, uint8_t token)
{
  // Wait for card ready before sending token/data
  if (!wait_ready(SD_RW_TIMEOUT_MS)) return 0;

  // Send token
  spi_txrx(token);

  // Stop token for multi-block write
  if (token == 0xFD) {
    // After stop token, card may go busy; wait until ready again
    if (!wait_ready(SD_RW_TIMEOUT_MS)) return 0;
    return 1;
  }

  // Data token must be followed by 512 bytes + 2 CRC bytes
  for (uint32_t i = 0; i < 512; i++) spi_txrx(buff[i]);

  // Dummy CRC
  spi_txrx(0xFF);
  spi_txrx(0xFF);

  // Read data response token
  uint8_t resp = spi_txrx(0xFF);
  if ((resp & 0x1F) != 0x05) return 0;   // 0x05 = accepted

  // IMPORTANT: wait for the internal write to complete (busy release)
  if (!wait_ready(SD_RW_TIMEOUT_MS)) return 0;

  return 1;
}

/* Send command packet */
static uint8_t send_cmd(uint8_t cmd, uint32_t arg)
{
  uint8_t crc = 0x01;

  // ACMD = CMD55 + CMD<n>
  if (cmd & 0x80) {
    cmd &= 0x7F;
    uint8_t r = send_cmd(55, 0);
    if (r > 1) return r;
  }

  SD_CS_HIGH();
  spi_txrx(0xFF);
  SD_CS_LOW();
  spi_txrx(0xFF);

  // Command
  spi_txrx(0x40 | cmd);
  spi_txrx((uint8_t)(arg >> 24));
  spi_txrx((uint8_t)(arg >> 16));
  spi_txrx((uint8_t)(arg >> 8));
  spi_txrx((uint8_t)arg);

  // CRC (needed for CMD0, CMD8)
  if (cmd == 0) crc = 0x95;
  if (cmd == 8) crc = 0x87;
  spi_txrx(crc);

  // Skip a byte when CMD12
  if (cmd == 12) spi_txrx(0xFF);

  // Wait response (response[7] == 0)
  uint8_t res;
  for (uint8_t n = 0; n < 10; n++) {
    res = spi_txrx(0xFF);
    if (!(res & 0x80)) break;
  }
  return res;
}

uint8_t sd_spi_get_type(void) { return sd_type; }

sd_status_t sd_spi_init(void)
{
  sd_type = 0;

  SD_CS_HIGH();
  spi_clock_dummy(20); // >=74 clocks (20 bytes = 160 clocks)

  uint32_t start = HAL_GetTick();

  // CMD0: go to idle
  uint8_t r = send_cmd(0, 0);
  if (r != 1) { SD_CS_HIGH(); return SD_NO_INIT; }

  // CMD8: check SDv2
  uint8_t ocr[4] = {0};
  r = send_cmd(8, 0x1AA);
  if (r == 1) {
    // Read R7
    for (int i = 0; i < 4; i++) ocr[i] = spi_txrx(0xFF);
    if (ocr[2] == 0x01 && ocr[3] == 0xAA) {
      // ACMD41 with HCS until ready
      do {
        r = send_cmd(0x80 | 41, 1UL << 30);
        if ((HAL_GetTick() - start) > SD_INIT_TIMEOUT_MS) { SD_CS_HIGH(); return SD_TIMEOUT; }
      } while (r != 0);

      // CMD58 read OCR
      r = send_cmd(58, 0);
      if (r != 0) { SD_CS_HIGH(); return SD_NO_INIT; }
      for (int i = 0; i < 4; i++) ocr[i] = spi_txrx(0xFF);

      sd_type = 0x01; // SDv2
      if (ocr[0] & 0x40) sd_type |= 0x02; // SDHC/SDXC
    }
  } else {
    // SDv1 or MMC: try ACMD41 (SD) then CMD1 (MMC)
    uint8_t is_sd = (send_cmd(0x80 | 41, 0) <= 1);
    do {
      if (is_sd) r = send_cmd(0x80 | 41, 0);
      else       r = send_cmd(1, 0);
      if ((HAL_GetTick() - start) > SD_INIT_TIMEOUT_MS) { SD_CS_HIGH(); return SD_TIMEOUT; }
    } while (r);

    // CMD16 set block length 512
    r = send_cmd(16, 512);
    if (r != 0) { SD_CS_HIGH(); return SD_NO_INIT; }
    sd_type = 0; // SDv1/MMC (byte addressing)
  }

  SD_CS_HIGH();
  spi_txrx(0xFF);
  return SD_OK;
}

sd_status_t sd_spi_read_sectors(uint8_t *buff, uint32_t sector, uint32_t count)
{
  if (count == 0) return SD_ERR;

  // Convert to byte address if not SDHC
  uint32_t addr = sector;
  if (!(sd_type & 0x02)) addr = sector * 512UL;

  if (count == 1) {
    uint8_t r = send_cmd(17, addr);
    if (r != 0) { SD_CS_HIGH(); return SD_ERR; }
    if (!recv_data_block(buff, 512)) { SD_CS_HIGH(); return SD_TIMEOUT; }
    SD_CS_HIGH();
    spi_txrx(0xFF);
    return SD_OK;
  } else {
    uint8_t r = send_cmd(18, addr);
    if (r != 0) { SD_CS_HIGH(); return SD_ERR; }
    do {
      if (!recv_data_block(buff, 512)) { SD_CS_HIGH(); return SD_TIMEOUT; }
      buff += 512;
    } while (--count);

    send_cmd(12, 0); // STOP_TRANSMISSION
    SD_CS_HIGH();
    spi_txrx(0xFF);
    return SD_OK;
  }
}

sd_status_t sd_spi_write_sectors(const uint8_t *buff, uint32_t sector, uint32_t count)
{
  if (count == 0) return SD_ERR;

  uint32_t addr = sector;
  if (!(sd_type & 0x02)) addr = sector * 512UL;

  if (count == 1) {
    uint8_t r = send_cmd(24, addr);
    if (r != 0) { SD_CS_HIGH(); return SD_ERR; }
    if (!xmit_data_block(buff, 0xFE)) { SD_CS_HIGH(); return SD_TIMEOUT; }
    SD_CS_HIGH();
    spi_txrx(0xFF);
    return SD_OK;
  } else {
    // Optional pre-erase for SD (ACMD23)
    if (sd_type & 0x01) send_cmd(0x80 | 23, count);

    uint8_t r = send_cmd(25, addr);
    if (r != 0) { SD_CS_HIGH(); return SD_ERR; }

    do {
      if (!xmit_data_block(buff, 0xFC)) { SD_CS_HIGH(); return SD_TIMEOUT; }
      buff += 512;
    } while (--count);

    if (!xmit_data_block(0, 0xFD)) { SD_CS_HIGH(); return SD_TIMEOUT; } // stop token

    SD_CS_HIGH();
    spi_txrx(0xFF);
    return SD_OK;
  }
}
