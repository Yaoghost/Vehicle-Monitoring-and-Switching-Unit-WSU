#ifndef LOGGER_H
#define LOGGER_H

#include <stdint.h>
#include "ff.h"

typedef struct {
  FATFS   *fs;
  FIL      file;
  uint8_t  mounted;
  uint8_t  opened;

  uint32_t last_log_ms;
  uint32_t last_flush_ms;
} logger_t;

FRESULT logger_init(logger_t *lg, FATFS *fs, const char *drive_path, const char *filename);

FRESULT logger_task(logger_t *lg,
                    uint32_t now_ms,
                    uint32_t period_ms,
                    float coolant_temp_f,
                    float oil_pressure_psi,
                    float fuel_level_pct);

FRESULT logger_close(logger_t *lg);

#endif // LOGGER_H
