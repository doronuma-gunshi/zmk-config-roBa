#define DT_DRV_COMPAT zmk_behavior_battery_print

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/battery.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <dt-bindings/zmk/keys.h>

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define TYPE_DELAY_MS 15  /* 1打鍵ごとの間隔。文字が抜ける場合は大きくする */
#define BUF_MAX 32

static uint32_t buf[BUF_MAX];
static int buf_len, buf_pos;
static bool key_down;
static struct k_work_delayable type_work;

/* 子機（左）の残量：子機から通知が来るたびに保持しておく */
static uint8_t periph_soc = 0xFF;  /* 0xFF = 未受信 */

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
static int periph_listener(const zmk_event_t *eh) {
    const struct zmk_peripheral_battery_state_changed *ev =
        as_zmk_peripheral_battery_state_changed(eh);
    if (ev != NULL && ev->source == 0) {
        periph_soc = ev->state_of_charge;
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(behavior_battery_print, periph_listener);
ZMK_SUBSCRIPTION(behavior_battery_print, zmk_peripheral_battery_state_changed);
#endif

static const uint32_t digit_keys[10] = {N0, N1, N2, N3, N4, N5, N6, N7, N8, N9};

static void push(uint32_t kc) {
    if (buf_len < BUF_MAX) buf[buf_len++] = kc;
}

static void push_level(uint8_t lv) {
    if (lv > 100) { push(MINUS); push(MINUS); return; } /* 未取得は "--" */
    if (lv >= 100) push(N1);
    if (lv >= 10) push(digit_keys[(lv / 10) % 10]);
    push(digit_keys[lv % 10]);
}

static void type_fn(struct k_work *work) {
    if (buf_pos >= buf_len) return;
    raise_zmk_keycode_state_changed_from_encoded(buf[buf_pos], !key_down, k_uptime_get());
    if (key_down) buf_pos++;
    key_down = !key_down;
    if (buf_pos < buf_len) k_work_schedule(&type_work, K_MSEC(TYPE_DELAY_MS));
}

static int on_pressed(struct zmk_behavior_binding *binding,
                      struct zmk_behavior_binding_event event) {
    if (buf_pos < buf_len) return ZMK_BEHAVIOR_OPAQUE; /* 入力中は無視 */
    buf_len = 0; buf_pos = 0; key_down = false;

    push(L); push_level(periph_soc);                    /* 左=子機 */
    push(SPACE);
    push(R); push_level(zmk_battery_state_of_charge()); /* 右=親機 */

    k_work_schedule(&type_work, K_NO_WAIT);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_released(struct zmk_behavior_binding *binding,
                       struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api api = {
    .binding_pressed = on_pressed,
    .binding_released = on_released,
};

static int bp_init(const struct device *dev) {
    k_work_init_delayable(&type_work, type_fn);
    return 0;
}

BEHAVIOR_DT_INST_DEFINE(0, bp_init, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &api);

#endif
