/*
 * Copyright (c) 2026 iwmsl
 * SPDX-License-Identifier: MIT
 *
 * Privacy boundary: this module stores aggregate counters only. It never
 * stores keycodes, characters, event order, timestamps, or host/app data.
 */

#define DT_DRV_COMPAT c4mtb_input_processor_telemetry

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <drivers/input_processor.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/matrix.h>

#define TELEMETRY_FORMAT_VERSION 1
#define TELEMETRY_LAYER_COUNT 6
#define TELEMETRY_POSITION_COUNT 38
#define TELEMETRY_POINTER_MODE_COUNT 3
#define TELEMETRY_BUTTON_COUNT 3

#define SETTINGS_ROOT "c4mtb_telemetry"
#define SETTINGS_META SETTINGS_ROOT "/meta"
#define SETTINGS_LAYERS SETTINGS_ROOT "/layers"
#define SETTINGS_BUTTONS SETTINGS_ROOT "/buttons"
#define SETTINGS_POINTER SETTINGS_ROOT "/pointer"

#define DIRTY_KEY_LAYER(layer) BIT(layer)
#define DIRTY_LAYER_ACTIVATIONS BIT(TELEMETRY_LAYER_COUNT)
#define DIRTY_BUTTONS BIT(TELEMETRY_LAYER_COUNT + 1)
#define DIRTY_POINTER BIT(TELEMETRY_LAYER_COUNT + 2)

BUILD_ASSERT(ZMK_KEYMAP_LAYERS_LEN == TELEMETRY_LAYER_COUNT,
             "Update C4MTB telemetry when the keymap layer count changes");
BUILD_ASSERT(ZMK_KEYMAP_LEN == TELEMETRY_POSITION_COUNT,
             "Update C4MTB telemetry when the physical position count changes");

struct telemetry_meta {
    uint16_t format_version;
    uint16_t layer_count;
    uint16_t position_count;
    uint16_t pointer_mode_count;
    uint32_t save_count;
    uint32_t overflow_count;
};

struct pointer_metrics {
    uint64_t absolute_x;
    uint64_t absolute_y;
    uint32_t axis_event_count;
};

struct telemetry_state {
    struct telemetry_meta meta;
    uint32_t key_press_count[TELEMETRY_LAYER_COUNT][TELEMETRY_POSITION_COUNT];
    uint32_t layer_activation_count[TELEMETRY_LAYER_COUNT];
    uint32_t mouse_button_count[TELEMETRY_BUTTON_COUNT];
    struct pointer_metrics pointer[TELEMETRY_POINTER_MODE_COUNT];
};

static struct telemetry_state state = {
    .meta = {
        .format_version = TELEMETRY_FORMAT_VERSION,
        .layer_count = TELEMETRY_LAYER_COUNT,
        .position_count = TELEMETRY_POSITION_COUNT,
        .pointer_mode_count = TELEMETRY_POINTER_MODE_COUNT,
    },
};
static struct telemetry_state save_snapshot;
static struct k_spinlock state_lock;
static struct k_work_delayable save_work;
#if IS_ENABLED(CONFIG_C4MTB_TELEMETRY_EXPORT)
static struct k_work_delayable export_work;
static struct telemetry_state export_snapshot;
#endif
static atomic_t dirty;
static atomic_t runtime_ready;
static uint32_t dirty_groups;
static bool meta_loaded;

static void reset_state(void) {
    memset(&state, 0, sizeof(state));
    state.meta.format_version = TELEMETRY_FORMAT_VERSION;
    state.meta.layer_count = TELEMETRY_LAYER_COUNT;
    state.meta.position_count = TELEMETRY_POSITION_COUNT;
    state.meta.pointer_mode_count = TELEMETRY_POINTER_MODE_COUNT;
}

static void count_u32(uint32_t *counter) {
    if (*counter < UINT32_MAX) {
        (*counter)++;
    } else if (state.meta.overflow_count < UINT32_MAX) {
        state.meta.overflow_count++;
    }
}

static void add_u64(uint64_t *counter, uint64_t amount) {
    if (UINT64_MAX - *counter >= amount) {
        *counter += amount;
    } else {
        *counter = UINT64_MAX;
        if (state.meta.overflow_count < UINT32_MAX) {
            state.meta.overflow_count++;
        }
    }
}

static void mark_dirty(uint32_t groups) {
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    dirty_groups |= groups;
    bool first_change = atomic_cas(&dirty, 0, 1);
    k_spin_unlock(&state_lock, key);

    if (first_change) {
        if (atomic_get(&runtime_ready)) {
            k_work_reschedule(&save_work,
                              K_MINUTES(CONFIG_C4MTB_TELEMETRY_SAVE_INTERVAL_MINUTES));
        }
    }
}

static int read_exact(settings_read_cb read_cb, void *cb_arg, void *value, size_t expected,
                      size_t actual) {
    if (actual != expected) {
        return -EINVAL;
    }

    ssize_t read = read_cb(cb_arg, value, expected);
    return read == (ssize_t)expected ? 0 : -EIO;
}

static int telemetry_settings_set(const char *name, size_t len, settings_read_cb read_cb,
                                  void *cb_arg) {
    const char *next;

    if (settings_name_steq(name, "meta", &next) && !next) {
        int err = read_exact(read_cb, cb_arg, &state.meta, sizeof(state.meta), len);
        meta_loaded = err == 0;
        return err;
    }

    if (settings_name_steq(name, "layers", &next) && !next) {
        return read_exact(read_cb, cb_arg, state.layer_activation_count,
                          sizeof(state.layer_activation_count), len);
    }

    if (settings_name_steq(name, "buttons", &next) && !next) {
        return read_exact(read_cb, cb_arg, state.mouse_button_count,
                          sizeof(state.mouse_button_count), len);
    }

    if (settings_name_steq(name, "pointer", &next) && !next) {
        return read_exact(read_cb, cb_arg, state.pointer, sizeof(state.pointer), len);
    }

    if (settings_name_steq(name, "keys", &next) && next) {
        char *end;
        unsigned long layer = strtoul(next, &end, 10);

        if (*next == '\0' || *end != '\0' || layer >= TELEMETRY_LAYER_COUNT) {
            return -ENOENT;
        }

        return read_exact(read_cb, cb_arg, state.key_press_count[layer],
                          sizeof(state.key_press_count[layer]), len);
    }

    return -ENOENT;
}

static int telemetry_settings_commit(void) {
    if (!meta_loaded || state.meta.format_version != TELEMETRY_FORMAT_VERSION ||
        state.meta.layer_count != TELEMETRY_LAYER_COUNT ||
        state.meta.position_count != TELEMETRY_POSITION_COUNT ||
        state.meta.pointer_mode_count != TELEMETRY_POINTER_MODE_COUNT) {
        reset_state();
    }

    atomic_set(&runtime_ready, 1);
    if (atomic_get(&dirty)) {
        k_work_reschedule(&save_work,
                          K_MINUTES(CONFIG_C4MTB_TELEMETRY_SAVE_INTERVAL_MINUTES));
    }
#if IS_ENABLED(CONFIG_C4MTB_TELEMETRY_EXPORT)
    k_work_reschedule(&export_work, K_SECONDS(3));
#endif

    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(c4mtb_telemetry, SETTINGS_ROOT, NULL, telemetry_settings_set,
                               telemetry_settings_commit, NULL);

static void remember_error(int *first_error, int error) {
    if (*first_error == 0 && error < 0) {
        *first_error = error;
    }
}

static void save_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    k_spinlock_key_t key = k_spin_lock(&state_lock);
    if (!atomic_cas(&dirty, 1, 0)) {
        k_spin_unlock(&state_lock, key);
        return;
    }

    uint32_t groups = dirty_groups;
    dirty_groups = 0;
    count_u32(&state.meta.save_count);
    memcpy(&save_snapshot, &state, sizeof(save_snapshot));
    k_spin_unlock(&state_lock, key);

    int error = 0;
    char setting_name[sizeof(SETTINGS_ROOT "/keys/00")];

    for (size_t layer = 0; layer < TELEMETRY_LAYER_COUNT; layer++) {
        if (!(groups & DIRTY_KEY_LAYER(layer))) {
            continue;
        }
        snprintk(setting_name, sizeof(setting_name), SETTINGS_ROOT "/keys/%u",
                 (unsigned int)layer);
        remember_error(&error,
                       settings_save_one(setting_name, save_snapshot.key_press_count[layer],
                                         sizeof(save_snapshot.key_press_count[layer])));
    }

    if (groups & DIRTY_LAYER_ACTIVATIONS) {
        remember_error(&error, settings_save_one(SETTINGS_LAYERS,
                                                 save_snapshot.layer_activation_count,
                                                 sizeof(save_snapshot.layer_activation_count)));
    }
    if (groups & DIRTY_BUTTONS) {
        remember_error(&error,
                       settings_save_one(SETTINGS_BUTTONS, save_snapshot.mouse_button_count,
                                         sizeof(save_snapshot.mouse_button_count)));
    }
    if (groups & DIRTY_POINTER) {
        remember_error(&error, settings_save_one(SETTINGS_POINTER, save_snapshot.pointer,
                                                 sizeof(save_snapshot.pointer)));
    }

    /* Meta is the commit marker and is intentionally written last. */
    remember_error(&error,
                   settings_save_one(SETTINGS_META, &save_snapshot.meta,
                                     sizeof(save_snapshot.meta)));

    if (error < 0) {
        key = k_spin_lock(&state_lock);
        dirty_groups |= groups;
        atomic_set(&dirty, 1);
        k_spin_unlock(&state_lock, key);
        k_work_reschedule(&save_work, K_MINUTES(1));
    }
}

int __real_raise_zmk_position_state_changed(struct zmk_position_state_changed data);

int __wrap_raise_zmk_position_state_changed(struct zmk_position_state_changed data) {
    if (!atomic_get(&runtime_ready) || !data.state ||
        data.position >= TELEMETRY_POSITION_COUNT) {
        return __real_raise_zmk_position_state_changed(data);
    }

    zmk_keymap_layer_index_t layer_index = zmk_keymap_highest_layer_active();
    zmk_keymap_layer_id_t layer_id = zmk_keymap_layer_index_to_id(layer_index);

    if (layer_id < TELEMETRY_LAYER_COUNT) {
        k_spinlock_key_t key = k_spin_lock(&state_lock);
        count_u32(&state.key_press_count[layer_id][data.position]);
        k_spin_unlock(&state_lock, key);
        mark_dirty(DIRTY_KEY_LAYER(layer_id));
    }

    return __real_raise_zmk_position_state_changed(data);
}

int __real_raise_zmk_layer_state_changed(struct zmk_layer_state_changed data);

int __wrap_raise_zmk_layer_state_changed(struct zmk_layer_state_changed data) {
    if (atomic_get(&runtime_ready) && data.state && data.layer < TELEMETRY_LAYER_COUNT) {
        k_spinlock_key_t key = k_spin_lock(&state_lock);
        count_u32(&state.layer_activation_count[data.layer]);
        k_spin_unlock(&state_lock, key);
        mark_dirty(DIRTY_LAYER_ACTIVATIONS);
    }

    return __real_raise_zmk_layer_state_changed(data);
}

static void mouse_button_input_callback(struct input_event *event, void *user_data) {
    ARG_UNUSED(user_data);

    if (!atomic_get(&runtime_ready) || event->type != INPUT_EV_KEY || event->value <= 0 ||
        event->code < INPUT_BTN_0 || event->code >= INPUT_BTN_0 + TELEMETRY_BUTTON_COUNT) {
        return;
    }

    size_t button = event->code - INPUT_BTN_0;
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    count_u32(&state.mouse_button_count[button]);
    k_spin_unlock(&state_lock, key);
    mark_dirty(DIRTY_BUTTONS);
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_NODELABEL(mkp)), mouse_button_input_callback, NULL);

static int telemetry_process(const struct device *device, struct input_event *event,
                             uint32_t mode, uint32_t param2,
                             struct zmk_input_processor_state *processor_state) {
    ARG_UNUSED(device);
    ARG_UNUSED(param2);
    ARG_UNUSED(processor_state);

    if (!atomic_get(&runtime_ready) || mode >= TELEMETRY_POINTER_MODE_COUNT ||
        event->type != INPUT_EV_REL) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    bool x_axis = event->code == INPUT_REL_X || event->code == INPUT_REL_HWHEEL;
    bool y_axis = event->code == INPUT_REL_Y || event->code == INPUT_REL_WHEEL;

    if (!x_axis && !y_axis) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    uint64_t magnitude =
        event->value < 0 ? (uint64_t)(-(int64_t)event->value) : (uint64_t)event->value;

    k_spinlock_key_t key = k_spin_lock(&state_lock);
    if (x_axis) {
        add_u64(&state.pointer[mode].absolute_x, magnitude);
    } else {
        add_u64(&state.pointer[mode].absolute_y, magnitude);
    }
    count_u32(&state.pointer[mode].axis_event_count);
    k_spin_unlock(&state_lock, key);
    mark_dirty(DIRTY_POINTER);

    return ZMK_INPUT_PROC_CONTINUE;
}

static const struct zmk_input_processor_driver_api telemetry_driver_api = {
    .handle_event = telemetry_process,
};

#define TELEMETRY_DEVICE(inst)                                                                 \
    DEVICE_DT_INST_DEFINE(inst, NULL, NULL, NULL, NULL, POST_KERNEL,                            \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &telemetry_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TELEMETRY_DEVICE)

#if IS_ENABLED(CONFIG_C4MTB_TELEMETRY_EXPORT)
#define TELEMETRY_EXPORT_LINE_BUFFER_SIZE 512
#define TELEMETRY_EXPORT_PACE_MS 50

static char export_line_buffer[TELEMETRY_EXPORT_LINE_BUFFER_SIZE];

static bool append_export_text(size_t *used, const char *text) {
    size_t remaining = sizeof(export_line_buffer) - *used;
    int written = snprintk(export_line_buffer + *used, remaining, "%s", text);

    if (written < 0 || (size_t)written >= remaining) {
        return false;
    }

    *used += (size_t)written;
    return true;
}

static bool append_export_value(size_t *used, const char *format, uint32_t value) {
    size_t remaining = sizeof(export_line_buffer) - *used;
    int written = snprintk(export_line_buffer + *used, remaining, format, value);

    if (written < 0 || (size_t)written >= remaining) {
        return false;
    }

    *used += (size_t)written;
    return true;
}

static void print_u32_array_line(const char *prefix, const uint32_t *values, size_t count,
                                 const char *suffix) {
    size_t used = 0;

    if (!append_export_text(&used, prefix) || !append_export_text(&used, "[")) {
        goto format_error;
    }

    for (size_t i = 0; i < count; i++) {
        if (!append_export_value(&used, i == 0 ? "%u" : ",%u", values[i])) {
            goto format_error;
        }
    }

    if (!append_export_text(&used, "]") || !append_export_text(&used, suffix)) {
        goto format_error;
    }

    printk("%s", export_line_buffer);
    k_msleep(TELEMETRY_EXPORT_PACE_MS);
    return;

format_error:
    printk("C4MTB_TELEMETRY_EXPORT_ERROR\n");
    k_msleep(TELEMETRY_EXPORT_PACE_MS);
}

static void export_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    k_spinlock_key_t key = k_spin_lock(&state_lock);
    memcpy(&export_snapshot, &state, sizeof(export_snapshot));
    k_spin_unlock(&state_lock, key);

    printk("C4MTB_TELEMETRY_BEGIN\n"
           "{\"format_version\":%u,\"layer_count\":%u,\"position_count\":%u,"
           "\"save_count\":%u,\"overflow_count\":%u,\n",
           export_snapshot.meta.format_version, export_snapshot.meta.layer_count,
           export_snapshot.meta.position_count, export_snapshot.meta.save_count,
           export_snapshot.meta.overflow_count);
    printk("\"key_press_count\":[\n");
    k_msleep(TELEMETRY_EXPORT_PACE_MS);
    for (size_t layer = 0; layer < TELEMETRY_LAYER_COUNT; layer++) {
        print_u32_array_line("", export_snapshot.key_press_count[layer],
                             TELEMETRY_POSITION_COUNT,
                             layer + 1 == TELEMETRY_LAYER_COUNT ? "\n],\n" : ",\n");
    }
    print_u32_array_line("\"layer_activation_count\":",
                         export_snapshot.layer_activation_count, TELEMETRY_LAYER_COUNT,
                         ",\n");
    print_u32_array_line("\"mouse_button_count\":", export_snapshot.mouse_button_count,
                         TELEMETRY_BUTTON_COUNT, ",\n");
    printk("\"pointer\":["
           "{\"absolute_x\":%llu,\"absolute_y\":%llu,\"axis_event_count\":%u},"
           "{\"absolute_x\":%llu,\"absolute_y\":%llu,\"axis_event_count\":%u},"
           "{\"absolute_x\":%llu,\"absolute_y\":%llu,\"axis_event_count\":%u}"
           "]}\nC4MTB_TELEMETRY_END\n",
           (unsigned long long)export_snapshot.pointer[0].absolute_x,
           (unsigned long long)export_snapshot.pointer[0].absolute_y,
           export_snapshot.pointer[0].axis_event_count,
           (unsigned long long)export_snapshot.pointer[1].absolute_x,
           (unsigned long long)export_snapshot.pointer[1].absolute_y,
           export_snapshot.pointer[1].axis_event_count,
           (unsigned long long)export_snapshot.pointer[2].absolute_x,
           (unsigned long long)export_snapshot.pointer[2].absolute_y,
           export_snapshot.pointer[2].axis_event_count);

    k_work_reschedule(&export_work, K_SECONDS(5));
}
#endif

static int telemetry_init(void) {
    k_work_init_delayable(&save_work, save_work_handler);
#if IS_ENABLED(CONFIG_C4MTB_TELEMETRY_EXPORT)
    k_work_init_delayable(&export_work, export_work_handler);
#endif
    return 0;
}

SYS_INIT(telemetry_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
