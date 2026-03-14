SD Card Modules for LPS
===

## 1. SD Mount API

### Mount SD Card

```c
esp_err_t mount_sdcard(void);
// example: mount_sdcard();
```

Mounts SD card to /sd directory. Must be called before any SD file operations

|  Return Value   |  Explaination |
|  :---  | :---  |
| ESP_OK  | Mount successful |
| ESP_FAIL  | Mount failed (no card, wrong format, etc.) |

### Unmount SD Card

```c
void unmount_sdcard(void);
// example: unmount_sdcard();
```

Unmounts SD card. Call before system shutdown.

### Get SD Card ID

```c
int get_sd_card_id(void);
// example: int id = get_sd_card_id();
```

Returns player ID from SD card volume label:

- Label "LPS01" ~ "LPS31" → returns 1~31
- Otherwise → returns 0

## 2. SD Logger API

### Initialization
```c
sd_log_init(const char* log_path);

// for example : esp_err_t sd_log_init("/sd/logger.log");
```

After initialization, all ESP_LOGx macros write to SD card.
Note: Must be called after SD card is mounted.

|  Return Value   |  Explaination |
|  :---  | :---  |
| ESP_OK  | Initialization successful |
| ESP_ERR_NO_MEM  | Memory allocation failed (ring buffer or mutex) |
| ESP_FAIL  | File open failed (SD card not mounted or path invalid) |

### Deinitialization
```c
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
```c
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


### How Logger Works

The logger uses a 4KB ring buffer to store log messages temporarily. When the buffer is full or during deinitialization, data is flushed to the log file in SD card. This approach:

- Reduces SD card wear by minimizing write operations
- Prevents task blocking during file I/O
- Uses thread-safe mutex for multi-tasking environments
