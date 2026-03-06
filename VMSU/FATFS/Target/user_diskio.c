/* USER CODE BEGIN Header */
/**
 ******************************************************************************
  * @file    user_diskio.c
  * @brief   USER Disk I/O driver for FatFs (SD card over SPI).
  *  Hardware assumptions (matches your setup):
  *   - SPI2 used for SD
  *   - CS is GPIO PB12 (must idle HIGH in MX_GPIO_Init)
 ******************************************************************************
  */
/* USER CODE END Header */

#ifdef USE_OBSOLETE_USER_CODE_SECTION_0
/*
 * Warning: the user section 0 is no more in use (starting from CubeMx version 4.16.0)
 * To be suppressed in the future.
 * Kept to ensure backward compatibility with previous CubeMx versions when
 * migrating projects.
 * User code previously added there should be copied in the new user sections before
 * the section contents can be deleted.
 */
/* USER CODE BEGIN 0 */
/* USER CODE END 0 */
#endif

/* USER CODE BEGIN DECL */

/* Includes ------------------------------------------------------------------*/
#include <string.h>
#include "ff_gen_drv.h"
#include "sd_spi.h"   // <-- YOU MUST HAVE THIS DRIVER

/* Private variables ---------------------------------------------------------*/
/* Disk status */
static volatile DSTATUS Stat = STA_NOINIT;

/* USER CODE END DECL */

/* Private function prototypes -----------------------------------------------*/
DSTATUS USER_initialize (BYTE pdrv);
DSTATUS USER_status     (BYTE pdrv);
DRESULT USER_read       (BYTE pdrv, BYTE *buff, DWORD sector, UINT count);
#if _USE_WRITE == 1
DRESULT USER_write      (BYTE pdrv, const BYTE *buff, DWORD sector, UINT count);
#endif /* _USE_WRITE == 1 */
#if _USE_IOCTL == 1
DRESULT USER_ioctl      (BYTE pdrv, BYTE cmd, void *buff);
#endif /* _USE_IOCTL == 1 */

Diskio_drvTypeDef USER_Driver =
{
  USER_initialize,
  USER_status,
  USER_read,
#if  _USE_WRITE
  USER_write,
#endif  /* _USE_WRITE == 1 */
#if  _USE_IOCTL == 1
  USER_ioctl,
#endif /* _USE_IOCTL == 1 */
};

/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Initializes a Drive
  * @param  pdrv: Physical drive number (0..)
  * @retval DSTATUS: Operation status
  */
DSTATUS USER_initialize (BYTE pdrv)
{
  (void)pdrv;

  if (sd_spi_init() == SD_OK)
  {
    Stat = 0;            // Clear STA_NOINIT -> Ready
  }
  else
  {
    Stat = STA_NOINIT;
  }

  return Stat;
}

/**
  * @brief  Gets Disk Status
  * @param  pdrv: Physical drive number (0..)
  * @retval DSTATUS: Operation status
  */
DSTATUS USER_status (BYTE pdrv)
{
  (void)pdrv;
  return Stat;
}

/**
  * @brief  Reads Sector(s)
  * @param  pdrv: Physical drive number (0..)
  * @param  buff: Data buffer to store read data
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to read (1..128)
  * @retval DRESULT: Operation result
  */
DRESULT USER_read (BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
  (void)pdrv;

  if (Stat & STA_NOINIT) return RES_NOTRDY;
  if (buff == NULL || count == 0) return RES_PARERR;

  return (sd_spi_read_sectors(buff, (uint32_t)sector, (uint32_t)count) == SD_OK)
         ? RES_OK : RES_ERROR;
}

/**
  * @brief  Writes Sector(s)
  * @param  pdrv: Physical drive number (0..)
  * @param  buff: Data to be written
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to write (1..128)
  * @retval DRESULT: Operation result
  */
#if _USE_WRITE == 1
DRESULT USER_write (BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
  (void)pdrv;

  if (Stat & STA_NOINIT) return RES_NOTRDY;
  if (buff == NULL || count == 0) return RES_PARERR;

  return (sd_spi_write_sectors(buff, (uint32_t)sector, (uint32_t)count) == SD_OK)
         ? RES_OK : RES_ERROR;
}
#endif /* _USE_WRITE == 1 */

/**
  * @brief  I/O control operation
  * @param  pdrv: Physical drive number (0..)
  * @param  cmd: Control code
  * @param  buff: Buffer to send/receive control data
  * @retval DRESULT: Operation result
  */
#if _USE_IOCTL == 1
DRESULT USER_ioctl (BYTE pdrv, BYTE cmd, void *buff)
{
  (void)pdrv;

  if (Stat & STA_NOINIT) return RES_NOTRDY;

  switch (cmd)
  {
    case CTRL_SYNC:
      // SD over SPI: writes are blocking in our driver; consider it synced
      return RES_OK;

    case GET_SECTOR_SIZE:
      *(WORD*)buff = 512;
      return RES_OK;

    case GET_BLOCK_SIZE:
      // Erase block size in units of sectors. Safe default:
      *(DWORD*)buff = 1;
      return RES_OK;

    case GET_SECTOR_COUNT:
      // Optional. FATFS can work without it in many cases, but some apps may ask.
      // We return "not supported" for now.
      return RES_ERROR;

    default:
      return RES_PARERR;
  }
}
#endif /* _USE_IOCTL == 1 */
