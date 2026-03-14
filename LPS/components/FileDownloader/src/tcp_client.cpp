#include "tcp_client.h"
#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "sd_writer.h"  
#include "bt_receiver.h"
#include "readframe.h"
#include "sd_mount.h"

static const char *TAG = "TCP_CLIENT";

/* Wi-Fi Event Group */
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

#define TCP_BUFFER_SIZE 16384

static int s_retry_num = 0;

extern QueueHandle_t sys_cmd_queue;
static esp_netif_t *s_wifi_netif = NULL;
static esp_event_handler_instance_t instance_any_id = NULL;
static esp_event_handler_instance_t instance_got_ip = NULL;
static bool s_is_stopping = false;

/* Wi-Fi Event Handler */
static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (!s_is_stopping && s_retry_num < 5) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/* Wi-Fi Initialization */
static void wifi_init_sta(void)
{
    // Create the event group to handle Wi-Fi events
    s_wifi_event_group = xEventGroupCreate();
    
    // Init TCP/IP stack and event loop
    esp_netif_init();
    esp_event_loop_create_default();

    // Create default Wi-Fi station
    if (s_wifi_netif == NULL) {
        s_wifi_netif = esp_netif_create_default_wifi_sta();
    }

    // Register event handlers for Wi-Fi and IP events
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    // Register event handlers for Wi-Fi and IP events
    if (instance_any_id == NULL) {
        esp_event_handler_instance_register(WIFI_EVENT,
                                            ESP_EVENT_ANY_ID,
                                            &event_handler,
                                            NULL,
                                            &instance_any_id);
    }
    
    // Register the event handler for when the device gets an IP address
    if (instance_got_ip == NULL) {
        esp_event_handler_instance_register(IP_EVENT,
                                            IP_EVENT_STA_GOT_IP,
                                            &event_handler,
                                            NULL,
                                            &instance_got_ip);
    }

    // Reset retry count and flags
    s_retry_num = 0;
    s_is_stopping = false;

    // Configure Wi-Fi connection settings
    wifi_config_t wifi_config = {};
    strcpy((char*)wifi_config.sta.ssid, TCP_WIFI_SSID);
    strcpy((char*)wifi_config.sta.password, TCP_WIFI_PASS);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    // Set Wi-Fi mode to station and start Wi-Fi
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_start();

    ESP_LOGI(TAG, "wifi_init_sta finished.");
}

/* Helper function to receive exact number of bytes */
static int recv_exact(int sock, void *buf, size_t len) {
    size_t received = 0;
    while (received < len) {
        int ret = recv(sock, (char *)buf + received, len - received, 0);
        if (ret <= 0) {
            return ret; 
        }
        received += ret;
    }
    return received;
}

/* Process to download a file from TCP server and (simulated) save to SD card */
static esp_err_t download_file(int sock, const char* filename) {
    uint32_t net_size = 0;
    
    // 1. Receive file size (4 bytes, network byte order)
    if (recv_exact(sock, &net_size, 4) <= 0) {
        ESP_LOGE(TAG, "Failed to receive size for %s", filename);
        return ESP_FAIL;
    }
    uint32_t file_size = ntohl(net_size);
    ESP_LOGI(TAG, "Downloading %s, Size: %lu bytes", filename, file_size);

    uint8_t *buf = (uint8_t *)malloc(TCP_BUFFER_SIZE);
    if (buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        return ESP_FAIL;
    }

#if LD_CFG_ENABLE_PT
    // 2. Initialize SD writer (only execute when SD card is enabled)
    if (sd_writer_init(filename) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init sd_writer for %s", filename);
        free(buf);
        return ESP_FAIL;
    }
#else
    ESP_LOGW(TAG, "SD Card disabled. Mocking write process for %s", filename);
#endif

    // 3. Receive file data in chunks
    size_t remaining = file_size;
    while (remaining > 0) {
        size_t to_read = (remaining < TCP_BUFFER_SIZE) ? remaining : TCP_BUFFER_SIZE;
        int n = recv_exact(sock, buf, to_read);
        if (n <= 0) {
            ESP_LOGE(TAG, "Socket error during download");
#if LD_CFG_ENABLE_PT
            sd_writer_close();
#endif
            free(buf);
            return ESP_FAIL;
        }

#if LD_CFG_ENABLE_PT
        // Perform real SD card write
        if (sd_writer_write(buf, n) != ESP_OK) {
            ESP_LOGE(TAG, "SD Write failed");
            sd_writer_close();
            free(buf);
            return ESP_FAIL;
        }
#else
        // Mock write delay (optional, slightly slow down reception to prevent buffer overload)
        // vTaskDelay(pdMS_TO_TICKS(5)); 
#endif
        remaining -= n;
    }

#if LD_CFG_ENABLE_PT
    sd_writer_close();
#endif
    free(buf);
    ESP_LOGI(TAG, "Download complete: %s", filename);
    return ESP_OK;
}

/* Update Task Function */
static void update_task_func(void *pvParameters) {
    ESP_LOGI(TAG, "=== Start Update Process ===");

    // [Step 1] Deinit BLE
    ESP_LOGI(TAG, ">>> Step 1: Deinit BLE");
    bt_receiver_deinit();
    vTaskDelay(pdMS_TO_TICKS(500)); 

    // [Step 2] Start Wi-Fi
    ESP_LOGI(TAG, ">>> Step 2: Start Wi-Fi");
    wifi_init_sta();

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Wi-Fi Connected. Connecting to TCP Server...");

        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = inet_addr(TCP_SERVER_IP);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(TCP_SERVER_PORT);

        int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        } else {
            int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
            if (err != 0) {
                ESP_LOGE(TAG, "Socket connect failed: errno %d", errno);
            } else {
                ESP_LOGI(TAG, "Connected to %s:%d", TCP_SERVER_IP, TCP_SERVER_PORT);

                // Optimize socket for bulk receive throughput (mirrors tcp_client reference)
                int rcvbuf = 65536;
                setsockopt(sock, SOL_SOCKET,  SO_RCVBUF,   &rcvbuf,  sizeof(rcvbuf));
                int nodelay = 1;
                setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

                // [Step 3] Message Player ID
#if LD_CFG_ENABLE_PT
                int pid = get_sd_card_id();
                if (pid <= 0) pid = 1; // Fallback protection
#else
                int pid = 1; // Force ID as 1 during test without SD card
#endif
                char msg[32];
                snprintf(msg, sizeof(msg), "%d\n", pid);
                send(sock, msg, strlen(msg), 0);
                ESP_LOGI(TAG, "Sent Player ID: %s", msg);

                // [Step 4] Download Files
                if (download_file(sock, "0:/control.dat") == ESP_OK) {
                    download_file(sock, "0:/frame.dat");
                }
                const char* ack_msg = "DONE\n";
                send(sock, ack_msg, strlen(ack_msg), 0);
            }
            close(sock);
        }
    } else {
        ESP_LOGE(TAG, "Wi-Fi connection failed.");
    }

    if (sys_cmd_queue != NULL) {
        sys_cmd_t msg = UPLOAD_SUCCESS;
        xQueueSend(sys_cmd_queue, &msg, 0);
        ESP_LOGI(TAG, "Message sent to main queue");
    }

    ESP_LOGI(TAG, "=== Update Process Finished, Task Deleting ===");
    vTaskDelete(NULL);
}

void tcp_client_start_update_task(void) {
    xTaskCreate(update_task_func, "tcp_update", 16384, NULL, 5, NULL);
}