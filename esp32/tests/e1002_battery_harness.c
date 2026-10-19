#include <assert.h>
#include "battery_adc.h"
#include "e1002_battery_curve.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali_scheme.h"

static int s_samples[10], s_reads, s_power, s_wait_ms, s_read_failure, s_cali_failure;
int gpio_config(const gpio_config_t *c) { assert(c->pin_bit_mask == (1ULL << 21)); return 0; }
int gpio_set_level(int gpio, int level) { assert(gpio == 21); s_power = level; return 0; }
void vTaskDelay(int ms) { assert(ms == 5 || ms == 25); s_wait_ms += ms; }
int adc_oneshot_io_to_channel(int gpio, int *unit, int *channel) {
    assert(gpio == 1); *unit = 1; *channel = 0; return 0;
}
int adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *c, void **out) {
    assert(c->unit_id == 1); *out = (void *)1; return 0;
}
int adc_oneshot_del_unit(void *unit) { assert(unit); return 0; }
int adc_oneshot_config_channel(void *u, int channel, const adc_oneshot_chan_cfg_t *c) {
    assert(u && channel == 0 && c->atten == 12); return 0;
}
int adc_cali_create_scheme_curve_fitting(const adc_cali_curve_fitting_config_t *c, void **out) {
    assert(c->unit_id == 1 && c->chan == 0); *out = (void *)2; return 0;
}
int adc_cali_raw_to_voltage(void *cali, int raw, int *mv) {
    assert(cali);
    if (s_cali_failure) return -1;
    *mv = raw; return 0;
}
int adc_oneshot_read(void *u, int channel, int *raw) {
    assert(u && channel == 0 && s_power && s_reads < 10);
    if (s_read_failure) return -1;
    *raw = s_samples[s_reads++]; return 0;
}
static void samples(int mv) {
    s_reads = 0; s_wait_ms = 0;
    for (int i = 0; i < 10; i++) s_samples[i] = mv;
}
int main(void) {
    int mv = -1, pct = -1;
    samples(1900);
    assert(battery_adc_read(&mv, &pct));
    assert(mv == 3800 && pct == 76 && s_reads == 10 && !s_power);
    assert(s_wait_ms == 5 + 9 * 25);
    assert(e1002_battery_percent(2795) == 0 && e1002_battery_percent(4111) == 100);
    assert(e1002_battery_percent(3642) == 50);
    assert(e1002_battery_percent(0) == 0 && e1002_battery_percent(5000) == 100);
    samples(1821); s_samples[0] = 1812; s_samples[1] = 1830;
    assert(battery_adc_read(&mv, &pct) && mv == 3642 && pct == 50);
    samples(1900); s_samples[0] = 2101; s_samples[1] = 1700;
    mv = -1; pct = -1;
    assert(!battery_adc_read(&mv, &pct) && !s_power && mv == -1 && pct == -1);
    samples(499);
    assert(!battery_adc_read(&mv, &pct) && !s_power);
    samples(1900); s_read_failure = 1;
    assert(!battery_adc_read(&mv, &pct) && !s_power);
    s_read_failure = 0; s_cali_failure = 1;
    assert(!battery_adc_read(&mv, &pct) && !s_power);
    return 0;
}
