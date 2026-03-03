#include "player.hpp"

#include "esp_check.h"
#include "esp_log.h"
#include "readframe.h"

static const char* TAG = "Player";

/* ================= Singleton ================= */

Player& Player::getInstance() {
    static Player instance;
    return instance;
}

Player::Player() = default;
Player::~Player() = default;

/* ================= Public lifecycle ================= */

esp_err_t Player::init() {
    ESP_RETURN_ON_FALSE(!taskAlive, ESP_ERR_INVALID_STATE, TAG, "player already started");
    return createTask();
}

esp_err_t Player::deinit() {
    Event e{};
    e.type = EVENT_EXIT;
    return sendEvent(e);
}

/* ================= External commands ================= */

esp_err_t Player::play() {
    Event e{};
    e.type = EVENT_PLAY;
    return sendEvent(e);
}

esp_err_t Player::pause() {
    Event e{};
    e.type = EVENT_PAUSE;
    return sendEvent(e);
}

esp_err_t Player::stop() {
    Event e{};
    e.type = EVENT_STOP;
    return sendEvent(e);
}

esp_err_t Player::release() {
    Event e{};
    e.type = EVENT_RELEASE;
    return sendEvent(e);
}

// esp_err_t Player::load() {
//     Event e{};
//     e.type = EVENT_LOAD;
//     return sendEvent(e);
// }

esp_err_t Player::test() {
    Event e{};
    e.type = EVENT_TEST;
    e.test_data.mode = BREATH_RGB;

    return sendEvent(e);
}
esp_err_t Player::test(uint8_t r, uint8_t g, uint8_t b) {
    Event e{};
    e.type = EVENT_TEST;
    e.test_data.mode = SOLID_RGB;

    e.test_data.r = r;
    e.test_data.g = g;
    e.test_data.b = b;
    return sendEvent(e);
}

esp_err_t Player::exit() {
    Event e{};
    e.type = EVENT_EXIT;
    return sendEvent(e);
}

esp_err_t Player::sdtest() {
    frame_system_deinit();
    frame_system_init("0:/control.dat", "0:/frame.dat");
    return ESP_OK;
}
/* ================= Playback control (called by State) ================= */

esp_err_t Player::startPlayback() {
    return clock.start();
}

esp_err_t Player::pausePlayback() {
    return clock.pause();
}

esp_err_t Player::resetPlayback() {
    ESP_RETURN_ON_ERROR(clock.pause(), TAG, "Failed to pause clock");
    ESP_RETURN_ON_ERROR(clock.reset(), TAG, "Failed to reset clock");
    ESP_RETURN_ON_ERROR(fb.reset(), TAG, "Failed to reset framebuffer");
    controller.fill(GRB_BLACK);
    controller.show();

    return ESP_OK;
}

esp_err_t Player::updatePlayback() {
    const uint64_t time_ms = clock.now_us() / 1000;

    FbComputeStatus fb_status = fb.compute(time_ms);
    if(fb_status == FbComputeStatus::ERROR_GENERAL) {
        ESP_LOGE(TAG, "framebuffer compute general error");
        Event e{};
        e.type = EVENT_STOP;
        ESP_RETURN_ON_ERROR(sendEvent(e), TAG, "stop event on framebuffer error");
        // return ESP_FAIL;
    }
    else if(fb_status == FbComputeStatus::ERROR_CRITICAL) {
        ESP_LOGE(TAG, "framebuffer compute critical error");
        Event e{};
        e.type = EVENT_RELEASE;
        ESP_RETURN_ON_ERROR(sendEvent(e), TAG, "release event on framebuffer critical error");
    }

    frame_data* buf = fb.get_buffer();
    controller.write_frame(buf);

    // print_frame_data(*buf);

    controller.show();

    if(fb_status == FbComputeStatus::EOF_REACHED) {
        Event e{};
        e.type = EVENT_STOP;
        ESP_RETURN_ON_ERROR(sendEvent(e), TAG, "failed to enqueue stop event on EOF");
    }

    return ESP_OK;
}

esp_err_t Player::testPlayback(TestData data) {
    if(data.mode == SOLID_RGB) {
        fb.set_test_mode(FbTestMode::SOLID);
        fb.set_test_color(grb8(data.r, data.g, data.b));
    }
    if(data.mode == BREATH_RGB) {
        fb.set_test_mode(FbTestMode::BREATH);
    }

    clock.start();

    return ESP_OK;
}

/* ================= RTOS ================= */

esp_err_t Player::createTask() {
    eventQueue = xQueueCreate(LD_CFG_PLAYER_EVENT_QUEUE_LEN, sizeof(Event));
    BaseType_t res = xTaskCreatePinnedToCore(Player::taskEntry, LD_CFG_PLAYER_TASK_NAME, LD_CFG_PLAYER_TASK_STACK_SIZE, NULL, LD_CFG_PLAYER_TASK_PRIORITY, &taskHandle, LD_CFG_PLAYER_TASK_CORE_ID);

    ESP_RETURN_ON_FALSE(res == pdPASS, ESP_FAIL, TAG, "create task failed");
    taskAlive = true;
    return ESP_OK;
}

void Player::taskEntry(void* pvParameters) {

    Player& p = Player::getInstance();

    Event bootEvent;
    bootEvent.type = EVENT_LOAD;
    p.processEvent(bootEvent);  // auto-load on start

    p.Loop();
}

void Player::Loop() {
    Event e{};
    uint32_t ulNotifiedValue;
    bool running = true;

    while(running) {
        xTaskNotifyWait(0, UINT32_MAX, &ulNotifiedValue, portMAX_DELAY);

        if(ulNotifiedValue & NOTIFICATION_EVENT) {
            while(xQueueReceive(eventQueue, &e, 0) == pdTRUE) {
                if(e.type == EVENT_EXIT) {
                    running = false;
                    break;
                }
                processEvent(e);
            }
        }

        if(running && (ulNotifiedValue & NOTIFICATION_UPDATE)) {
            updateState();
        }
    }

    releaseResources();
    if(eventQueue) {
        vQueueDelete(eventQueue);
        eventQueue = nullptr;
    }
    taskAlive = false;
    ESP_LOGI(TAG, "player task exit");
    vTaskDelete(NULL);
}

/* ================= Event sending ================= */

esp_err_t Player::sendEvent(Event& event) {
    ESP_RETURN_ON_FALSE(taskAlive && eventQueue != nullptr, ESP_ERR_INVALID_STATE, TAG, "player not ready");
    ESP_RETURN_ON_FALSE(xQueueSend(eventQueue, &event, 0) == pdTRUE, ESP_ERR_TIMEOUT, TAG, "event queue full");
    xTaskNotify(taskHandle, NOTIFICATION_EVENT, eSetBits);
    return ESP_OK;
}

esp_err_t Player::acquireResources() {
    if(resources_acquired) {
        return ESP_OK;
    }

    ESP_RETURN_ON_FALSE(eventQueue != nullptr, ESP_ERR_NO_MEM, TAG, "eventQueue is NULL");
    ESP_RETURN_ON_ERROR(controller.init(), TAG, "controller init failed");
    ESP_RETURN_ON_ERROR(fb.init(), TAG, "framebuffer init failed");
    ESP_RETURN_ON_FALSE(LD_CFG_PLAYER_FPS > 0, ESP_ERR_INVALID_ARG, TAG, "invalid player fps");
    ESP_RETURN_ON_ERROR(clock.init(true, taskHandle, 1000000 / LD_CFG_PLAYER_FPS), TAG, "clock init failed");

    resources_acquired = true;
    return ESP_OK;
}

esp_err_t Player::releaseResources() {
    if(!resources_acquired) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(clock.deinit(), TAG, "clock deinit failed");
    ESP_RETURN_ON_ERROR(fb.deinit(), TAG, "framebuffer deinit failed");
    ESP_RETURN_ON_ERROR(controller.deinit(), TAG, "controller deinit failed");

    resources_acquired = false;
    return ESP_OK;
}
