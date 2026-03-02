// sd_logger.h
#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t sd_log_init(const char* path);
esp_err_t sd_log_deinit(void);

#ifdef __cplusplus
}
#endif