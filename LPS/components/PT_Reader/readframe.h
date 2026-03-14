#pragma once

#include <stdbool.h>
#include "esp_err.h"

#include "ld_frame.h"
#ifdef __cplusplus
extern "C" {
#endif

extern ch_info_t ch_info_snapshot;

/* ============================================================
 * Frame System (SD -> Frame Reader -> Player)
 *
 * 使用流程：
 *
 *   frame_system_init("0:/control.dat", "0:/frame.dat");
 *
 *   while (read_frame(&frame) == ESP_OK) {
 *       // play frame
 *   }
 *
 *   frame_reset();   // optional
 *
 *   frame_system_deinit();
 * ============================================================ */

/**
 * @brief 初始化整個 frame system
 *
 * 會完成：
 *   - 讀取 control.dat → ch_info
 *   - 初始化 frame_reader
 *   - 建立 SD reader task
 *
 * @param control_path  control.dat 路徑（例如 "0:/control.dat"）
 * @param frame_path    frame.dat 路徑（例如 "0:/frame.dat"）
 *
 * @return
 *   - ESP_OK
 *   - ESP_ERR_INVALID_STATE  已初始化
 *   - ESP_ERR_NOT_FOUND      SD / 檔案不存在
 *   - ESP_ERR_NO_MEM         semaphore / task 建立失敗
 *   - ESP_FAIL               其他錯誤
 */
esp_err_t frame_system_init(const char* control_path, const char* frame_path);

/**
 * @brief 讀取下一個 frame（blocking）
 *
 * @param[out] out  caller 提供的 frame buffer
 *
 * @return
 *   - ESP_OK                成功
 *   - ESP_ERR_INVALID_STATE 尚未 init
 *   - ESP_ERR_INVALID_ARG   out 為 NULL
 *   - ESP_ERR_NOT_FOUND     EOF（沒有 frame 了）
 */
esp_err_t read_frame(table_frame_t* out);

/**
 * @brief 重置播放位置到 frame 0
 *
 * 非同步命令，實際 reset 由 SD reader task 處理
 *
 * @return
 *   - ESP_OK
 *   - ESP_ERR_INVALID_STATE 尚未 init
 */
esp_err_t frame_reset(void);

/**
 * @brief 關閉 frame system 並釋放所有資源
 *
 * 安全可重入
 */
esp_err_t frame_system_deinit(void);

bool is_eof_reached(void);

#ifdef __cplusplus
}
#endif
