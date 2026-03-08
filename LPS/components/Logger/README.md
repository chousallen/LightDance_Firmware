SD Card Logger for LPS
===

## 1. API usage

### Initialization
```
sd_log_init(const char* log_path);

// for example : esp_err_t sd_log_init("/sd/logger.log");
```

After initialization, all ESP_LOGx macros re-direct write to *log_path in SD card.

This API must be called after the SD card is successfully mounted. In LPS, this means calling it after frame_system_init() which handles the SD card mounting process.

|  Return Value   |  Explaination |
|  :---  | :---  |
| ESP_OK  | Initialization successful |
| ESP_ERR_NO_MEM  | Memory allocation failed (ring buffer or mutex) |
| ESP_FAIL  | File open failed (SD card not mounted or path invalid) |

### Deinitialization
```
sd_log_deinit(void);

// for example : esp_err_t sd_log_deinit(void);
```

Call this API when SD logging is no longer needed, typically before system shutdown or SD card unmount. 
It flushes remaining data from ring buffer to file and releases resources.

|  Return Value   |  Explaination |
|  :---  | :---  |
| ESP_OK  | Deinitialization successful |
| ESP_ERR_INVALID_STATE  | Logger was not initialized |

### Flush
```
esp_err_t sd_log_flush(void);

// for example : sd_log_flush();
```
Manually force write buffered logs from ring buffer to SD card.
Use this API when you need to ensure logs are written immediately:

- Before system reset or deep sleep
- At critical checkpoints
- After important log messages

|  Return Value   |  Explaination |
|  :---  | :---  |
| ESP_OK  | Flush successful |
| ESP_ERR_INVALID_STATE  | Logger was not initialized |

## 2. How It Works

The logger uses a 4KB ring buffer to store log messages temporarily. When the buffer is full or during deinitialization, data is flushed to the log file in SD card. This approach:

- Reduces SD card wear by minimizing write operations
- Prevents task blocking during file I/O
- Uses thread-safe mutex for multi-tasking environments