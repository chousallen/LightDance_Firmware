# Pattern Table Reader System v1.2 Guide 

This document describes the architecture, finite state machine (FSM), APIs, and expected behaviors of the Pattern Table Reader System.

The system is designed to safely read control.dat and frame.dat from SD card with deterministic state transitions and strict error handling.

## 1. Finite State Machine

define variable
```
static bool inited; 
static bool running; 
static bool eof_reached;
```

The finite state machine of pattern table reader system:

```mermaid
stateDiagram-v2
direction TB
    
    UNINIT --> INITED: frame_system_init()

    INITED --> ACTIVE: read_frame()

    note right of INITED: frame_reset() remain in INITED
    
    note right of ACTIVE: read_frame() remain in ACTIVE
    
    ACTIVE --> EOF: read_frame() return EOF

    ACTIVE --> INITED: frame_reset()

    ACTIVE --> UNINIT: frame_system_deinit()

    note right of EOF: read_frame() remain in EOF
    
    EOF --> INITED: frame_reset()
    
    EOF --> UNINIT: frame_system_deinit()

    INITED --> UNINIT: frame_system_deinit()

    note right of STOPPED: internal fatal error
```

Description of each state :


| State       | Description                         | Internal Flags                                   |
| ----------- | ----------------------------------- | ------------------------------------------------ |
| **UNINIT**  | System not initialized              | inited = 0 <br> running = 0 <br> eof_reached = 0 |
| **INITED**  | System initialized, ready to read   | inited = 1 <br> running = 0 <br> eof_reached = 0 |
| **ACTIVE**  | Reader is actively reading frames   | inited = 1 <br> running = 1 <br> eof_reached = 0 |
| **EOF**     | End of frame.dat reached            | inited = 1 <br> running = 1 <br> eof_reached = 1 |


## 2. FSM API

### 1. frame_system_init(const char* control_path, const char* frame_path)

Initialize the pattern table reader system

|  Current state   |  Next state   | Return |
|  :---  | :---  | :---  |
| UNINIT  | INITED | ESP_OK |
| INITED  | INITED | ESP_ERR_INVALID_STATE |
| ACTIVE  | ACTIVE | ESP_ERR_INVALID_STATE |
| EOF  | EOF | ESP_ERR_INVALID_STATE |

<br>

- Detail Error Code Reference

| Return                   | Description                           |
| ------------------------ | ------------------------------------- |
| ESP_ERR_INVALID_STATE    | System already initialized            |
| ESP_ERR_INVALID_ARG      | control_path or frame_path invalid    |
| ESP_ERR_NOT_FOUND        | control.dat or frame.dat missing      |
| ESP_FAIL                 | I/O error or version mismatch         |
| ESP_ERR_INVALID_RESPONSE | control.dat format invalid            |
| ESP_ERR_NO_MEM           | Memory allocation failed              |
| ESP_ERR_INVALID_SIZE     | Frame size exceeds FRAME_RAW_MAX_SIZE |
| ESP_ERR_INVALID_CRC      | control.dat checksum mismatch         |

---

### 2. read_frame(table_frame_t* playerbuffer)

Reading next frame data

|  Current state   |  Next state   | Return |
|  :---  | :---  | :---  |
| UNINIT  | UNINIT | ESP_ERR_INVALID_STATE |
| INITED  | ACTIVE | ESP_OK |
| ACTIVE  | ACTIVE | ESP_OK |
| ACTIVE  | EOF | ESP_ERR_NOT_FOUND |
| EOF  | EOF | ESP_ERR_NOT_FOUND |

<br>

- Detail Error Code Reference

|  Return type   |  Description |
|  :---  | :---  |
| ESP_ERR_INVALID_STATE  | System not initialized (UNINIT state) |
| ESP_ERR_NOT_FOUND  | No more frames to read, system enters EOF state |
| ESP_ERR_INVALID_SIZE | Frame file corrupted: incomplete frame read (expected size mismatch) |
| ESP_ERR_INVALID_ARG  | Invalid playerbuffer or NULL |
| ESP_ERR_INVALID_CRC  | Checksum mismatch in frame.dat |
| ESP_FAIL | Reader task stopped or I/O error |

---

### 3. frame_reset(void)

Reset reader pointer back to start of frame.dat (frame 0)

|  Current state   |  Next state   | Return |
|  :---  | :---  | :---  |
| UNINIT  | UNINIT | ESP_ERR_INVALID_STATE |
| INITED  | INITED | ESP_OK |
| ACTIVE  | INITED | ESP_OK |
| EOF  | INITED | ESP_OK |

---

### 4. frame_system_deinit(void)

Leave and close the pattern table reader system, release resource

|  Current state   |  Next state   | Return |
|  :---  | :---  | :---  |
| UNINIT  | UNINIT | ESP_ERR_INVALID_STATE |
| INITED  | UNINIT | ESP_OK |
| ACTIVE  | UNINIT | ESP_OK |
| EOF  | UNINIT | ESP_OK |
| STOPPED  | UNINIT | ESP_OK |

## 3. Other API

### is_eof_reached(void)

- return eof_reached ( True / False )
