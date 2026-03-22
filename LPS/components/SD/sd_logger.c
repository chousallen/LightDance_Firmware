#include "sd_logger.h"
#include "sd_utils.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "unistd.h"
#include <sys/stat.h>

#define BUFFER_SIZE (4*1024)      // ring buffer size
#define TEMP_BUFFER_SIZE 256         //single logger size
static const char* TAG = "LOGGER";

typedef struct {
    char data[BUFFER_SIZE];          // data buffer
    uint32_t head;                    // write position
    uint32_t tail;                    // read position
    SemaphoreHandle_t mutex;
    FILE* file;
    bool running;
    TaskHandle_t task;
} ring_buffer_t;

static ring_buffer_t* g_buf = NULL;
static vprintf_like_t orig_vprintf = NULL;

static void flush_buffer(void) {
    if (!g_buf->file || g_buf->head == g_buf->tail) return;
    
    size_t written = 0;
    if (g_buf->head > g_buf->tail) {
        written = fwrite(&g_buf->data[g_buf->tail], 1, 
                        g_buf->head - g_buf->tail, g_buf->file);
    } else {
        written = fwrite(&g_buf->data[g_buf->tail], 1, 
                        BUFFER_SIZE - g_buf->tail, g_buf->file);
        written += fwrite(g_buf->data, 1, g_buf->head, g_buf->file);
    }
    
    if (written > 0) {
        g_buf->tail = (g_buf->tail + written) % BUFFER_SIZE;
        fflush(g_buf->file);
        fsync(fileno(g_buf->file)); 
    }
}

static bool immediate_log(const char* fmt) {
    if (!fmt) return
        false;

    char first_char = fmt[0];
    if(first_char == 'E' || first_char == 'W')
        return true;
    return false;
}

static int ring_buffer_write(const char* fmt, va_list args) {
    if (!g_buf || !g_buf->running) return 0;
    
    if (immediate_log(fmt)) {
        // warning or error: direct flush into sd card
        xSemaphoreTake(g_buf->mutex, portMAX_DELAY);
        if (g_buf->file) {
            vfprintf(g_buf->file, fmt, args);
            fflush(g_buf->file);
            fsync(fileno(g_buf->file));
        }
        xSemaphoreGive(g_buf->mutex);
        return 0;
    }
    else {
        // not warning or error: into ring buffer
        char temp[TEMP_BUFFER_SIZE];
        int len = vsnprintf(temp, sizeof(temp), fmt, args);
        if (len <= 0) return 0;
        
        xSemaphoreTake(g_buf->mutex, portMAX_DELAY);
        
        for (int i = 0; i < len; i++) {
            uint32_t next = (g_buf->head + 1) % BUFFER_SIZE;
            g_buf->data[g_buf->head] = temp[i];
            g_buf->head = next;
        }

        xSemaphoreGive(g_buf->mutex);
        return len;
    }
}

static void flush_task(void* arg) {
    while (g_buf->running) {
        xSemaphoreTake(g_buf->mutex, portMAX_DELAY);
        flush_buffer();
        xSemaphoreGive(g_buf->mutex);
        
        vTaskDelay(50);
    }
    vTaskDelete(NULL);
}

esp_err_t sd_log_init() {
    ESP_LOGI(TAG, "logger initializing");
    g_buf = heap_caps_malloc(sizeof(ring_buffer_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        
    if (!g_buf) return ESP_ERR_NO_MEM;
    memset(g_buf, 0, sizeof(ring_buffer_t));
    
    g_buf->mutex = xSemaphoreCreateMutex();
    if (!g_buf->mutex) {
        free(g_buf);
        return ESP_ERR_NO_MEM;
    }

    //update logger name while reset
    //use 8.3 filename for FATfs
    char path[32];
    int index = 1;
    struct stat st;
    while (1) {
        snprintf(path, sizeof(path), "/sd/log%d.log", index); //don't exceed 8 char for logXXX.log
        if (stat(path, &st) != 0) {
            break;
        }
        index++;
    }
    g_buf->file = fopen(path, "w+");
    if (!g_buf->file) {
        vSemaphoreDelete(g_buf->mutex);
        free(g_buf);
        return ESP_FAIL;
    }
    fflush(g_buf->file);
    g_buf->running = true;
    ESP_LOGI(TAG, "log path: %s", path);
    
    xTaskCreate(flush_task, "sd_logger_flush", 2048, NULL, 1, &g_buf->task);
    orig_vprintf = esp_log_set_vprintf(ring_buffer_write);

    vTaskDelay(pdMS_TO_TICKS(100));
    return ESP_OK;
}

esp_err_t sd_log_deinit(void) {
    if (!g_buf) return ESP_ERR_INVALID_STATE;
    
    g_buf->running = false;
    vTaskDelay(pdMS_TO_TICKS(50));

    if (g_buf->file) {
        xSemaphoreTake(g_buf->mutex, portMAX_DELAY);
        flush_buffer();
        xSemaphoreGive(g_buf->mutex);
        fclose(g_buf->file);
    }
    
    esp_log_set_vprintf(orig_vprintf);
    
    if (g_buf->mutex) vSemaphoreDelete(g_buf->mutex);
    free(g_buf);
    g_buf = NULL;
    
    return ESP_OK;
}

esp_err_t sd_log_flush(void) {
    if (!g_buf || !g_buf->running) return ESP_ERR_INVALID_STATE;
    
    xSemaphoreTake(g_buf->mutex, portMAX_DELAY);
    flush_buffer();
    xSemaphoreGive(g_buf->mutex);
    
    return ESP_OK;
}