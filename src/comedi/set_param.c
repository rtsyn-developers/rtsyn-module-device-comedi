#include <math.h>
#include <string.h>

#include "rtsyn/internal/comedi.h"
#include "rtsyn/internal/comedi/set_param.h"

rtsyn_abi_status_t RTSYN_ABI_CALL rtsyn_comedi_set_param(void *instance, uint32_t param_index,
                                                         const void *value) {
    rtsyn_comedi_instance_t *comedi = (rtsyn_comedi_instance_t *)instance;

    if (!comedi || !value) {
        return RTSYN_ABI_STATUS_INVALID_ARGUMENT;
    }

    switch (param_index) {
    case RTSYN_COMEDI_PARAM_DEVICE_PATH:
        strncpy(comedi->device_path, (const char *)value, sizeof(comedi->device_path) - 1);
        comedi->device_path[sizeof(comedi->device_path) - 1] = '\0';
        return RTSYN_ABI_STATUS_OK;
    default: {
        double number = *(const double *)value;
        if (!isfinite(number)) return RTSYN_ABI_STATUS_INVALID_ARGUMENT;
        uint32_t bases[] = {RTSYN_COMEDI_PARAM_ANALOG_OUTPUT_GAIN_BASE,
            rtsyn_comedi_analog_input_gain_base, rtsyn_comedi_analog_output_offset_base,
            rtsyn_comedi_analog_input_offset_base};
        uint32_t counts[] = {rtsyn_comedi_analog_output_count, rtsyn_comedi_analog_input_count,
            rtsyn_comedi_analog_output_count, rtsyn_comedi_analog_input_count};
        double *values[] = {comedi->analog_output_gains, comedi->analog_input_gains,
            comedi->analog_output_offsets, comedi->analog_input_offsets};
        for (uint32_t group = 0; group < 4; ++group) {
            if (param_index >= bases[group] && param_index - bases[group] < counts[group]) {
                values[group][param_index - bases[group]] = number;
                return RTSYN_ABI_STATUS_OK;
            }
        }
        return RTSYN_ABI_STATUS_INVALID_ARGUMENT;
    }
    }
}
