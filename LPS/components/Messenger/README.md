# BlueTooth Receiver Component (ESP32)

This is a Bluetooth Low Energy (BLE) receiver component designed for the ESP32. It bypasses standard Bluedroid or NimBLE stacks, communicating directly with the controller via the ESP32's **HCI (Host Controller Interface)**. This achieves ultra-low latency, precise parsing of advertising packets, and includes a **Status Reporting mechanism**.

Its primary function is for multi-device synchronization systems. The receiver scans for specific BLE advertising packets, parses commands, synchronizes triggers using a windowing algorithm, and provides real-time status feedback upon request.

## ✨ Features

* **Low Latency Parsing**: Performs rapid parsing of advertising packets (`fast_parse_and_trigger`) directly within the VHCI callback function (ISR context).
* **Precise Synchronization**: Includes synchronization window logic (`sync_process_task`) to collect multiple advertising packets and calculate the average trigger time, eliminating the variance caused by wireless transmission latency.
* **Visual Acknowledgement**: Immediately turns all LEDs **RED** upon receiving a valid `PLAY` command. This provides instant visual confirmation that the signal was received.
* **Status Feedback**: Responds to `CHECK` commands by reporting the current state and the dynamic remaining time until the next action.
* **Target Filtering**: Supports filtering by `Manufacturer ID` and `Target Mask` (bitmask), allowing commands to be targeted at a single device or a group of devices.
* **Decoupled System Control**: Utilizes a dedicated FreeRTOS Queue (`sys_cmd_queue`) to safely handle heavy system operations (like Wi-Fi initialization or reboots) without blocking the primary Bluetooth reception loop.

## 🧠 System Internal Workflow

This section explains how the receiver processes a signal from the air to the actual action execution.

### 1. Packet Reception (ISR Context)

* **HCI Callback**: When the Bluetooth Controller receives an advertising packet, it triggers `vhci_host_cb`.
* **Fast Parsing**: The function `fast_parse_and_trigger` filters the packet by Manufacturer ID and Target Mask.
* **Queueing**: If valid, the raw packet data (including the `delay`, `rssi`, and `rx_timestamp`) is pushed into a FreeRTOS Queue (`s_adv_queue`).

### 2. Synchronization (Task Context)

The `sync_process_task` continuously reads from the queue:

* **Window Start**: When a new Command ID is seen, a "Sync Window" (e.g., 500ms) starts.
* **Averaging**: The task collects multiple packets for the same Command ID. It calculates the **Absolute Target Time** for each packet: `Target = RX_Timestamp + Delay_Value`.
* **Jitter Reduction**: It averages these Target Times to calculate the final firing timestamp, effectively canceling out transmission jitter.

### 3. Visual Acknowledgement (Visual ACK)

* **Trigger Condition**: When a **new** `PLAY` command (ID changes) is successfully locked.
* **Action**: The system immediately sets all LEDs to **RED** (`RGB: 255, 0, 0`).
* **Duration**: The red light remains active for the duration specified in the packet's `prep_time`.
* **Constraint**: The `prep_time` MUST be set to at least **1.0 second** (1,000,000 us) to ensure visibility.
* **Anti-Spam**: A state array (`s_visual_ack_done`) ensures the red light is triggered only once per command ID, preventing flickering caused by the sender's round-robin broadcasting.

### 4. Scheduling & Execution

* **Timer Set**: Once the window closes, `esp_timer_start_once` is called with the calculated remaining duration.
* **Action Trigger**: When the timer expires, `timer_timeout_cb` executes the corresponding Player action (Play, Pause, etc.).
* **System Commands**: If the command is a system-level action (e.g., `UPLOAD` or `RESET`), the receiver packages the command data and pushes it to the `sys_cmd_queue` for safe execution in a separate task context, preventing interference with precise timing operations.
* **Cancel Logic**: If a `CANCEL` command is received:
1. It stops the timer for the targeted Command ID.
2. **Smart LED Control**: The Visual ACK (Red Light) is turned off **ONLY if** the canceled command was a `PLAY` command. Canceling other commands (e.g., PAUSE) will not affect the Red Light.



## 📂 File Structure

```text
Messenger/
├── CMakeLists.txt          # ESP-IDF component build script
├── include/
│   └── bt_receiver.h       # External API interface and structure definitions
└── src/
    └── bt_receiver.cpp     # Core implementation (HCI commands, ISR parsing, sync logic, ACK task)
```

## 🛠 Dependencies

* **ESP-IDF** (Must include `bt`, `nvs_flash`, `esp_timer`, and other standard components).
* **Player Module** (`player.hpp`): This component relies on an external `Player` singleton class to execute the actual actions (e.g., `play()`, `pause()`, `test()`).

## 🚀 Usage

### 1. Configuration and Initialization

Include the header file and initialize the component in `main.cpp`. It is highly recommended to set up the `sys_cmd_queue` and its associated processing task before initializing the Bluetooth receiver to handle system-level commands properly.

```c
#include "bt_receiver.h"
#include "freertos/queue.h"

// Define the global system command queue
QueueHandle_t sys_cmd_queue = NULL;

static void app_task(void* arg) {
    // ... Player initialization ...

    // Initialize the System Command Queue using the enum type
    sys_cmd_queue = xQueueCreate(10, sizeof(sys_cmd_t));
    if (sys_cmd_queue != NULL) {
        xTaskCreate(sys_cmd_task, "sys_cmd_task", 4096, NULL, 5, NULL);
    }
// ...
```

### 2. Stop Receiving

If you need to stop scanning and timers:

```c
bt_receiver_stop();
```

### 3. De-initialization (Wi-Fi Coexistence)

To completely release Bluetooth resources (e.g., before connecting to Wi-Fi to save memory or avoid hardware conflicts), use `deinit`. This function disables the controller and frees all allocated tasks and queues.

```c
// 1. Fully stop and release BLE resources
bt_receiver_deinit();

// ... Perform Wi-Fi operations (e.g., OTA update, File Download) ...

// 2. Re-initialize when done with Wi-Fi
bt_receiver_init(&config);
bt_receiver_start();
```

## 📡 Protocol Definition

### 1. Received Packet (From Sender)

The receiver parses the `AD Type = 0xFF` (Manufacturer Specific Data) section within the BLE advertising packet.

| Offset | Length | Description | Notes |
| --- | --- | --- | --- |
| 0 | 3 | **Manufacturer ID** | Little Endian, must match Config |
| 3 | 2 |  **unique code (LD)** | `0x4C, 0x44` |
| 5 | 1 | **CMD Info** | High 4-bit: `CMD_ID` (Identifier), Low 4-bit: `CMD_TYPE` (Action Type) |
| 6 | 8 | **Target Mask** | 64-bit Mask, corresponds to `my_player_id` |
| 14 | 4 | **Delay** | Big Endian, execution delay (us) |

The remaining bytes: 
* `PLAY`

| Offset | Length | Value | Description |
| --- | --- | --- | --- |
| **18** | 4 | `prep_led_ms` | Preparation Time (Big Endian) |

* `TEST`

| Offset | Length | Value | Description |
| --- | --- | --- | --- |
| **18** | 3 | `data[3]` | RGB |
| **21** | 1 | `0` | padding |

* `CANCEL`

| Offset | Length | Value | Description |
| --- | --- | --- | --- |
| **18** | 1 | `cmd_id` | the cmd id that you want to cancel |
| **19** | 3 | `0` | padding |

* `SEEK`

| Offset | Length | Value | Description |
| --- | --- | --- | --- |
| **18** | 4 | `target_time_ms` | The specific timeline position to seek to (Big Endian) |

**Total Length**: 22 Bytes.

### 2. Transmitted ACK Packet (To Sender)

When a `CHECK` command is received, the receiver broadcasts an ACK packet.

| Offset | Length | Description | Notes |
| --- | --- | --- | --- |
| 0 | 2 | **Manufacturer ID** | `0xFFFF` |
| 2 | 1 | **Packet Type** | **`0x07`** (CMD_TYPE_ACK) |
| 3 | 1 | **My ID** | The `my_player_id` of this device |
| 4 | 1 | **CMD ID** | ID of the command being acknowledged |
| 5 | 1 | **CMD Type** | Action type (e.g., PLAY, PAUSE) |
| 6 | 4 | **Delay** | Big Endian, the locked delay value |
| 10 | 1 | **State** | Current Player State (e.g., READY, PLAYING) |

### Supported Command Types (CMD_TYPE)

These values are defined in the `timer_timeout_cb` function within `bt_receiver.cpp`:

| Type Code | Enum (`lps_cmd_t`) | Action | Data | Visual ACK |
| --- | --- | --- | --- | --- |
| `0x01` | `LPS_CMD_PLAY` | **Play** | None | **YES** (RED) |
| `0x02` | `LPS_CMD_PAUSE` | **Pause** | None | No |
| `0x03` | `LPS_CMD_STOP` | **Stop** | None | No |
| `0x04` | `LPS_CMD_RELEASE` | **Release** | None | No |
| `0x05` | `LPS_CMD_TEST` | **Test** | Uses `Data[0-2]` for RGB | No |
| `0x06` | `LPS_CMD_CANCEL` | **Cancel** | `Data[0]` contains the target `CMD_ID` to cancel | Stops LED **only** if canceling PLAY |
| `0x07` | `LPS_CMD_CHECK` | **Check** | None | No |
| `0x08` | `LPS_CMD_UPLOAD` | **Upload** | None | YES (GREEN) |
| `0x09` | `LPS_CMD_RESET` | **Reset** | None | No |
| `0x09` | `LPS_CMD_SEEK` | **Seek** | None | No |