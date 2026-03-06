#include "logger.h"
#include <string.h>
#include <stdio.h>

/*
 * Flush policy:
 *  - Set to 0 to sync EVERY write (best for bring-up/testing).
 *  - Set to e.g. 10000 for flush every 10s (less wear).
 */
#ifndef LOGGER_FLUSH_EVERY_MS
#define LOGGER_FLUSH_EVERY_MS 0UL
#endif

#ifndef LOG_LINE_MAX
#define LOG_LINE_MAX 128
#endif

static int file_is_empty(FIL *f)
{
  return (f_size(f) == 0U) ? 1 : 0;
}

FRESULT logger_init(logger_t *lg, FATFS *fs, const char *drive_path, const char *filename)
{
  if (!lg || !fs || !drive_path || !filename) return FR_INVALID_OBJECT;

  memset(lg, 0, sizeof(*lg));
  lg->fs = fs;

  // 1) Mount
  FRESULT res = f_mount(lg->fs, drive_path, 1);
  if (res != FR_OK) return res;
  lg->mounted = 1;

  // 2) Build path "0:/log.csv" (supports drive_path like "0:" or "0:/")
  char path[64];
  size_t dlen = strlen(drive_path);

  if (dlen > 0 && drive_path[dlen - 1] == '/') {
    snprintf(path, sizeof(path), "%s%s", drive_path, filename);   // "0:/log.csv"
  } else {
    snprintf(path, sizeof(path), "%s/%s", drive_path, filename);  // "0:/log.csv"
  }

  // 3) Open in append mode (create if missing)
  res = f_open(&lg->file, path, FA_OPEN_ALWAYS | FA_WRITE);
  if (res != FR_OK) return res;
  lg->opened = 1;

  // 4) Seek to end for append
  res = f_lseek(&lg->file, f_size(&lg->file));
  if (res != FR_OK) return res;

  // 5) If new file, write CSV header
  if (file_is_empty(&lg->file)) {
    const char *hdr = "time_ms,coolant_temp_f,oil_pressure_psi,fuel_level_pct\r\n";
    UINT bw = 0;
    res = f_write(&lg->file, hdr, (UINT)strlen(hdr), &bw);
    if (res != FR_OK) return res;
    if (bw != (UINT)strlen(hdr)) return FR_DISK_ERR;

    res = f_sync(&lg->file);
    if (res != FR_OK) return res;
  }

  // Force "log immediately" on first task call:
  lg->last_log_ms   = 0U;
  lg->last_flush_ms = 0U;

  return FR_OK;
}

FRESULT logger_task(logger_t *lg,
                    uint32_t now_ms,
                    uint32_t period_ms,
                    float coolant_temp_f,
                    float oil_pressure_psi,
                    float fuel_level_pct)
{
  if (!lg || !lg->mounted || !lg->opened) return FR_NOT_READY;

  // Log immediately first time, then every period_ms
  if (lg->last_log_ms != 0U) {
    if ((uint32_t)(now_ms - lg->last_log_ms) < period_ms) return FR_OK;
  }
  lg->last_log_ms = now_ms;

  char line[LOG_LINE_MAX];
  int n = snprintf(line, sizeof(line),
                   "%lu,%.2f,%.2f,%.2f\r\n",
                   (unsigned long)now_ms,
                   coolant_temp_f,
                   oil_pressure_psi,
                   fuel_level_pct);

  if (n <= 0) return FR_INT_ERR;
  if ((size_t)n >= sizeof(line)) return FR_INVALID_PARAMETER; // line truncated

  UINT bw = 0;
  FRESULT res = f_write(&lg->file, line, (UINT)n, &bw);
  if (res != FR_OK) return res;
  if (bw != (UINT)n) return FR_DISK_ERR;

  // Flush policy
  if (LOGGER_FLUSH_EVERY_MS == 0UL) {
    res = f_sync(&lg->file);
    if (res != FR_OK) return res;
    lg->last_flush_ms = now_ms;
  } else {
    if ((uint32_t)(now_ms - lg->last_flush_ms) >= LOGGER_FLUSH_EVERY_MS) {
      res = f_sync(&lg->file);
      if (res != FR_OK) return res;
      lg->last_flush_ms = now_ms;
    }
  }

  return FR_OK;
}

FRESULT logger_close(logger_t *lg)
{
  if (!lg) return FR_INVALID_OBJECT;

  FRESULT res = FR_OK;

  if (lg->opened) {
    (void)f_sync(&lg->file);
    res = f_close(&lg->file);
    lg->opened = 0;
  }

  if (lg->mounted) {
    (void)f_mount(0, "", 0);
    lg->mounted = 0;
  }

  return res;
}
