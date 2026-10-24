#include "battery.h"

#include "board.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"

namespace {

// Releases whatever was created, on every return path.
struct Adc {
    adc_oneshot_unit_handle_t unit = nullptr;
    adc_cali_handle_t cali = nullptr;

    ~Adc()
    {
        if (cali) {
            adc_cali_delete_scheme_curve_fitting(cali);
        }
        if (unit) {
            adc_oneshot_del_unit(unit);
        }
    }
};

}  // namespace

namespace battery {

int read_mv()
{
    adc_unit_t unit_id;
    adc_channel_t channel;
    if (adc_oneshot_io_to_channel(board::PIN_VBAT_SENSE, &unit_id, &channel) != ESP_OK) {
        return -1;
    }

    Adc adc;
    adc_oneshot_unit_init_cfg_t unit_cfg = {.unit_id = unit_id};
    if (adc_oneshot_new_unit(&unit_cfg, &adc.unit) != ESP_OK) {
        return -1;
    }
    adc_oneshot_chan_cfg_t chan_cfg = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    adc_oneshot_config_channel(adc.unit, channel, &chan_cfg);

    // Corrects the ADC itself (factory eFuse data), not the divider: see VBAT_DIVIDER_NUM.
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = unit_id, .chan = channel, .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &adc.cali) != ESP_OK) {
        return -1;
    }

    int sum = 0;
    const int samples = 8;
    for (int i = 0; i < samples; i++) {
        int raw, mv;
        adc_oneshot_read(adc.unit, channel, &raw);
        adc_cali_raw_to_voltage(adc.cali, raw, &mv);
        sum += mv;
    }
    return (sum / samples) * board::VBAT_DIVIDER_NUM / board::VBAT_DIVIDER_DEN;
}

}  // namespace battery
