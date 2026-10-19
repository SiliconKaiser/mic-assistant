#include "battery.h"

#include "board.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"

int battery_read_mv(void)
{
    adc_unit_t unit;
    adc_channel_t channel;
    if (adc_oneshot_io_to_channel(PIN_VBAT_SENSE, &unit, &channel) != ESP_OK) {
        return -1;
    }

    adc_oneshot_unit_handle_t adc;
    adc_oneshot_unit_init_cfg_t unit_cfg = {.unit_id = unit};
    if (adc_oneshot_new_unit(&unit_cfg, &adc) != ESP_OK) {
        return -1;
    }
    adc_oneshot_chan_cfg_t chan_cfg = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    adc_oneshot_config_channel(adc, channel, &chan_cfg);

    adc_cali_handle_t cali;
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = unit, .chan = channel, .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali) != ESP_OK) {
        adc_oneshot_del_unit(adc);
        return -1;
    }

    int sum = 0;
    const int samples = 8;
    for (int i = 0; i < samples; i++) {
        int raw, mv;
        adc_oneshot_read(adc, channel, &raw);
        adc_cali_raw_to_voltage(cali, raw, &mv);
        sum += mv;
    }

    adc_cali_delete_scheme_curve_fitting(cali);
    adc_oneshot_del_unit(adc);
    return (sum / samples) * VBAT_DIVIDER_NUM / VBAT_DIVIDER_DEN;
}
