#define DT_DRV_COMPAT c4mtb_behavior_cursor_speed

#include <zephyr/device.h>

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#include <drivers/behavior.h>

#define MOUSE_PROCESSOR DT_NODELABEL(mouse_runtime_input_processor)

#if DT_NODE_HAS_STATUS(MOUSE_PROCESSOR, okay) && IS_ENABLED(CONFIG_ZMK_RUNTIME_INPUT_PROCESSOR)
#include <zmk/pointing/input_processor_runtime.h>
#endif

static int cursor_speed_pressed(struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);

#if DT_NODE_HAS_STATUS(MOUSE_PROCESSOR, okay) && IS_ENABLED(CONFIG_ZMK_RUNTIME_INPUT_PROCESSOR)
    const struct device *mouse = DEVICE_DT_GET(MOUSE_PROCESSOR);
    const uint32_t standard_multiplier = DT_PROP(MOUSE_PROCESSOR, scale_multiplier);
    const uint32_t standard_divisor = DT_PROP(MOUSE_PROCESSOR, scale_divisor);
    struct zmk_input_processor_runtime_config config;

    int err = zmk_input_processor_runtime_get_config(mouse, NULL, &config);
    if (err < 0) {
        return err;
    }

    /* A gain saved by Studio may use a different fraction for the same speed. */
    bool at_standard = (uint64_t)config.scale_multiplier * standard_divisor ==
                       (uint64_t)config.scale_divisor * standard_multiplier;
    err = zmk_input_processor_runtime_set_scaling(
        mouse, at_standard ? 3 : standard_multiplier, at_standard ? 2 : standard_divisor,
        ZMK_INPUT_PROCESSOR_RUNTIME_WRITE_MODE_PERSIST);
    if (err < 0) {
        return err;
    }
#endif

    return ZMK_BEHAVIOR_OPAQUE;
}

static int cursor_speed_released(struct zmk_behavior_binding *binding,
                                 struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
}

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
static const struct behavior_parameter_metadata metadata = {0};
#endif

static const struct behavior_driver_api cursor_speed_api = {
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
    .binding_pressed = cursor_speed_pressed,
    .binding_released = cursor_speed_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif
};

#define CURSOR_SPEED_INST(n)                                                                        \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                                  \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &cursor_speed_api);

DT_INST_FOREACH_STATUS_OKAY(CURSOR_SPEED_INST)

#endif
