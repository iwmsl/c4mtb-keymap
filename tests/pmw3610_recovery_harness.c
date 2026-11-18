/* Host tests execute functions extracted from the patched driver, not a model. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#define K_NO_WAIT 0
#define K_FOREVER -1
#define K_MSEC(x) (x)
#define K_SECONDS(x) ((x)*1000)
#define K_USEC(x) (x)
#define LOG_WRN(...) ((void)0)
#define LOG_DBG(...) ((void)0)
#define unlikely(x) (x)
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define CONTAINER_OF(ptr, type, member) ((type *)((char *)(ptr)-offsetof(type, member)))
#define CLAMP(x, lo, hi) ((x)<(lo)?(lo):((x)>(hi)?(hi):(x)))
#define PMW3610_PRODUCT_ID 0x3e
#define PMW3610_REG_PRODUCT_ID 0
#define PMW3610_REG_MOTION_BURST 0x12
#define PMW3610_REG_MOTION 2
#define PMW3610_REG_SPI_CLK_ON_REQ 0x41
#define PMW3610_SPI_CLOCK_CMD_ENABLE 0xba
#define PMW3610_SPI_CLOCK_CMD_DISABLE 0xb5
#define T_CLOCK_ON_DELAY_US 300
#define PMW3610_BURST_SIZE 7
#define PMW3610_X_L_POS 1
#define PMW3610_Y_L_POS 2
#define PMW3610_XY_H_POS 3
#define PMW3610_SHUTTER_H_POS 5
#define PMW3610_SHUTTER_L_POS 6
#define ZMK_ACTIVITY_ACTIVE 0

struct k_work { int unused; };
struct k_work_delayable { struct k_work work; };
struct k_mutex { int held; };
struct pixart_runtime_config {
    uint32_t report_interval_min_ms;
    bool swap_xy, invert_x, invert_y, smart_algorithm, force_awake;
};
struct pixart_config { bool disable_burst_read; int evt_type, x_input_code, y_input_code, irq_gpio; };
struct device { void *data; const struct pixart_config *config; };
struct pixart_data {
    const struct device *dev;
    struct k_mutex lock;
    bool ready, capture_active, suspended, sw_smart_flag;
    struct pixart_runtime_config runtime;
    struct k_work trigger_work;
    struct k_work_delayable init_work, health_work;
    unsigned int recovery_errors;
    int async_init_step, async_init_retry_count, err;
    int64_t dx, dy, last_smp_time, last_rpt_time;
};
struct zmk_activity_state_changed { int state; };
typedef struct zmk_activity_state_changed zmk_event_t;
static struct pixart_data d;
static struct pixart_config cfg;
static struct device dev = { &d, &cfg };
static const struct device *pmw3610_devs[] = { &dev };
static const int async_init_delay[] = {10};
static int read_error, irq_error, motion, product, submitted, resets, delay;
static int writes, fail_write, input_count, perf_count;
static bool irq_enabled;
static uint8_t burst[7];
static int64_t now = 100;
static int k_mutex_lock(struct k_mutex *m, int wait) {
    if (wait == K_NO_WAIT && m->held) return -EBUSY;
    ++m->held;
    return 0;
}
static void k_mutex_unlock(struct k_mutex *m) { assert(m->held); --m->held; }
static void k_sleep(int delay) {}
static int64_t k_uptime_get(void) { return now; }
static struct k_work_delayable *k_work_delayable_from_work(struct k_work *w) {
    return CONTAINER_OF(w, struct k_work_delayable, work);
}
static int k_work_reschedule(struct k_work_delayable *w, int ms) {
    if (w == &d.init_work) ++resets;
    else delay = ms;
    return 0;
}
static int k_work_submit(struct k_work *w) { ++submitted; return 1; }
static int gpio_pin_get_dt(const int *pin) { return motion; }
static int pmw3610_set_interrupt(const struct device *dev, bool enable) {
    irq_enabled = enable && !irq_error;
    return irq_error;
}
static int pmw3610_read_reg(const struct device *dev, uint8_t addr, uint8_t *value) {
    assert(d.lock.held);
    *value = product;
    return read_error;
}
static int pmw3610_read(const struct device *dev, uint8_t addr, uint8_t *buf, uint8_t len) {
    assert(d.lock.held);
    memcpy(buf, burst + (len == 1 ? addr - PMW3610_REG_MOTION : 0), len);
    return read_error;
}
static int pmw3610_write_reg(const struct device *dev, uint8_t addr, uint8_t value) {
    assert(d.lock.held);
    return ++writes == fail_write ? -EIO : 0;
}
static int input_report(const struct device *dev, int type, int code, int value, bool sync, int wait) {
    assert(!d.lock.held); /* Activity callback must be safe to re-enter. */
    ++input_count;
    return 0;
}
static int pmw3610_set_performance(const struct device *dev, bool force, bool enable) {
    assert(d.lock.held);
    ++perf_count;
    return 0;
}
static struct zmk_activity_state_changed *as_zmk_activity_state_changed(const zmk_event_t *ev) {
    return (struct zmk_activity_state_changed *)ev;
}

/* DRIVER_FUNCTIONS */

static void reset(void) {
    memset(&d, 0, sizeof(d));
    memset(&cfg, 0, sizeof(cfg));
    memset(burst, 0, sizeof(burst));
    d.dev = &dev;
    d.ready = true;
    read_error = irq_error = motion = submitted = resets = delay = 0;
    writes = fail_write = input_count = perf_count = 0;
    product = PMW3610_PRODUCT_ID;
    irq_enabled = false;
}
static void health(void) { pmw3610_health_check(&d.health_work.work); assert(!d.lock.held); }
static void report(void) { pmw3610_work_callback(&d.trigger_work); assert(!d.lock.held); }
int main(void) {
    reset();
    for (int i=0; i<100; ++i) health();
    assert(!resets && !submitted && irq_enabled); /* stationary is not a fault */
    motion = 1; health();
    assert(submitted == 1); /* lost IRQ is rescued */
    burst[1] = 4; report();
    assert(input_count == 1 && irq_enabled && !d.recovery_errors);

    reset(); read_error = -EIO;
    report(); assert(!irq_enabled && delay == 20 && d.recovery_errors == 1);
    health(); assert(!resets && d.recovery_errors == 2);
    health(); assert(resets == 1 && !d.ready);
    assert(!d.dx && !d.dy && !d.sw_smart_flag);

    reset(); read_error = -EIO; report(); read_error = 0;
    health(); assert(submitted == 1);
    report(); assert(!resets && irq_enabled && !d.recovery_errors);

    reset(); product = 0xff;
    health(); health(); health();
    assert(resets == 1 && !d.ready);
    reset(); irq_error = -EIO;
    report(); report(); report(); assert(resets == 1);
    reset(); motion = -EIO;
    health(); health(); health(); assert(resets == 1);

    reset(); d.capture_active = true;
    health(); report(); assert(!submitted && !resets && !irq_enabled && !input_count);
    reset(); d.suspended = true;
    health(); assert(!delay && !submitted && !resets);
    reset(); d.lock.held = 1;
    pmw3610_health_check(&d.health_work.work);
    assert(delay == 1000 && !resets); d.lock.held = 0;
    reset(); d.ready = false; d.err = -EIO; d.async_init_retry_count = 10;
    health(); assert(resets == 1 && !d.async_init_retry_count);
    reset(); d.ready = false; d.async_init_retry_count = 2;
    health(); assert(!resets);

    for (int stage=1; stage<=3; ++stage) {
        reset(); fail_write = stage; d.lock.held = 1;
        assert(pmw3610_write(&dev, 0x32, 0x80) == -EIO);
        assert(writes == (stage == 1 ? 2 : 3)); /* cleanup after every failure */
        d.lock.held = 0;
    }
    reset(); d.runtime.smart_algorithm = true; burst[6] = 46; fail_write = 2;
    report(); assert(!d.sw_smart_flag && d.recovery_errors == 1);
    reset(); cfg.disable_burst_read = true; read_error = -EIO;
    report(); assert(!d.lock.held && d.recovery_errors == 1);
    reset(); d.runtime.report_interval_min_ms = 12; d.last_rpt_time = now;
    burst[1] = 4; report(); assert(!d.lock.held && !input_count);
    reset(); zmk_event_t active = { ZMK_ACTIVITY_ACTIVE };
    on_activity_state(&active); assert(perf_count == 1 && !d.lock.held);
    d.capture_active = true;
    on_activity_state(&active); assert(perf_count == 1);
    puts("PASS: real PMW3610 recovery functions, injected SPI/GPIO/init/capture faults");
    return 0;
}
