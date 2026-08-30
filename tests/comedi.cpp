#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>

#include "rtsyn/comedi.h"
#include "rtsyn/internal/comedi.h"

namespace {

struct HostContext {
    std::array<std::array<std::uint8_t, sizeof(uint64_t)>, RTSYN_COMEDI_MAX_PORTS> inputs{};
    std::array<std::array<std::uint8_t, sizeof(uint64_t)>, RTSYN_COMEDI_MAX_PORTS> outputs{};
    std::array<bool, RTSYN_COMEDI_MAX_PORTS> output_written{};
    std::array<bool, RTSYN_COMEDI_MAX_PORTS> connected{};
    std::array<std::uint32_t, RTSYN_COMEDI_MAX_PORTS> read_count{};
};

std::size_t value_size(uint32_t port_index) {
    const auto *descriptor = rtsyn_comedi_get_descriptor();
    if (port_index >= descriptor->port_count) return 0;
    return descriptor->ports[port_index].value_type == RTSYN_ABI_VALUE_F64 ? sizeof(double)
                                                                           : sizeof(uint64_t);
}

rtsyn_abi_status_t read(const rtsyn_abi_runtime_context_t *context, uint32_t port_index,
                        void *out_value) {
    if (out_value == nullptr) {
        return RTSYN_ABI_STATUS_INVALID_ARGUMENT;
    }
    const auto *host = static_cast<const HostContext *>(context->host_context);
    if (port_index >= host->inputs.size()) {
        return RTSYN_ABI_STATUS_INVALID_ARGUMENT;
    }
    const std::size_t size = value_size(port_index);
    if (size == 0) {
        return RTSYN_ABI_STATUS_INVALID_ARGUMENT;
    }
    std::memcpy(out_value, host->inputs[port_index].data(), size);
    const_cast<HostContext *>(host)->read_count[port_index]++;
    return RTSYN_ABI_STATUS_OK;
}

rtsyn_abi_status_t write(const rtsyn_abi_runtime_context_t *context, uint32_t port_index,
                         const void *value) {
    if (value == nullptr) {
        return RTSYN_ABI_STATUS_INVALID_ARGUMENT;
    }
    auto *host = const_cast<HostContext *>(static_cast<const HostContext *>(context->host_context));
    if (port_index >= host->outputs.size()) {
        return RTSYN_ABI_STATUS_INVALID_ARGUMENT;
    }
    const std::size_t size = value_size(port_index);
    if (size == 0) {
        return RTSYN_ABI_STATUS_INVALID_ARGUMENT;
    }
    std::memcpy(host->outputs[port_index].data(), value, size);
    host->output_written[port_index] = true;
    return RTSYN_ABI_STATUS_OK;
}

bool is_connected(const rtsyn_abi_runtime_context_t *context, uint32_t port_index) {
    const auto *host = static_cast<const HostContext *>(context->host_context);
    return port_index < host->connected.size() && host->connected[port_index];
}

TEST(ComediDevice, DescribesAbiSurface) {
    const auto *descriptor = rtsyn_comedi_get_descriptor();

    ASSERT_NE(descriptor, nullptr);
    EXPECT_STREQ(descriptor->name, "comedi");
    EXPECT_EQ(descriptor->node_type, RTSYN_ABI_NODE_DEVICE);
    ASSERT_GT(descriptor->port_count, 0U);
    ASSERT_LE(descriptor->port_count, RTSYN_COMEDI_MAX_PORTS);
    EXPECT_NE(std::strstr(descriptor->ports[0].name, "_0"), nullptr);
    ASSERT_GE(descriptor->param_count, 2U);
    EXPECT_STREQ(descriptor->params[0].name, "device_path");
    EXPECT_STREQ(descriptor->params[1].name, "analog_output_gain_0");
    ASSERT_EQ(descriptor->state_count, 2U);
    EXPECT_STREQ(descriptor->states[0].name, "hardware_ready");
    EXPECT_STREQ(descriptor->states[1].name, "last_error");
}

TEST(ComediDevice, AnalogInputGainsAreFloatingPointAndValidated) {
    const auto *descriptor = rtsyn_comedi_get_descriptor();
    void *instance = nullptr;
    ASSERT_EQ(descriptor->callbacks.create(&instance), RTSYN_ABI_STATUS_OK);
    auto *self = static_cast<rtsyn_comedi_instance_t *>(instance);
    uint32_t channel = 0;
    for (uint32_t param = 0; param < descriptor->param_count; ++param) {
        if (std::strncmp(descriptor->params[param].name, "analog_input_gain_", 18) != 0) continue;
        EXPECT_EQ(descriptor->params[param].value_type, RTSYN_ABI_VALUE_F64);
        EXPECT_DOUBLE_EQ(self->analog_input_gains[channel], 1.0);
        double gain = 0.001;
        EXPECT_EQ(descriptor->callbacks.set_param(instance, param, &gain), RTSYN_ABI_STATUS_OK);
        EXPECT_DOUBLE_EQ(self->analog_input_gains[channel], gain);
        gain = __builtin_nan("");
        EXPECT_EQ(descriptor->callbacks.set_param(instance, param, &gain), RTSYN_ABI_STATUS_INVALID_ARGUMENT);
        EXPECT_DOUBLE_EQ(self->analog_input_gains[channel], 0.001);
        ++channel;
    }
    EXPECT_GT(channel, 0U);
    descriptor->callbacks.destroy(instance);
}

TEST(ComediDevice, OffsetsDefaultToZeroAndRejectNonFiniteValues) {
    const auto *descriptor = rtsyn_comedi_get_descriptor();
    void *instance = nullptr;
    ASSERT_EQ(descriptor->callbacks.create(&instance), RTSYN_ABI_STATUS_OK);
    auto *self = static_cast<rtsyn_comedi_instance_t *>(instance);
    for (uint32_t input = 0; input < 2; ++input) {
        auto base = input ? rtsyn_comedi_analog_input_offset_base : rtsyn_comedi_analog_output_offset_base;
        auto count = input ? rtsyn_comedi_analog_input_count : rtsyn_comedi_analog_output_count;
        auto *offsets = input ? self->analog_input_offsets : self->analog_output_offsets;
        for (uint32_t channel = 0; channel < count; ++channel) {
            EXPECT_EQ(descriptor->params[base + channel].value_type, RTSYN_ABI_VALUE_F64);
            EXPECT_DOUBLE_EQ(offsets[channel], 0.0);
            double offset = -0.25;
            EXPECT_EQ(descriptor->callbacks.set_param(instance, base + channel, &offset), RTSYN_ABI_STATUS_OK);
            EXPECT_DOUBLE_EQ(offsets[channel], -0.25);
            offset = __builtin_inf();
            EXPECT_EQ(descriptor->callbacks.set_param(instance, base + channel, &offset), RTSYN_ABI_STATUS_INVALID_ARGUMENT);
            EXPECT_DOUBLE_EQ(offsets[channel], -0.25);
        }
    }
    double offset = 0.0;
    EXPECT_EQ(descriptor->callbacks.set_param(instance, descriptor->param_count, &offset), RTSYN_ABI_STATUS_INVALID_ARGUMENT);
    descriptor->callbacks.destroy(instance);
}

TEST(ComediDevice, CreatesStartsProcessesStopsAndDestroys) {
    const auto *descriptor = rtsyn_comedi_get_descriptor();
    void *instance = nullptr;
    HostContext host{};
    const rtsyn_abi_runtime_api_t api{read, write};
    const rtsyn_abi_runtime_context_t immediate_context{
        0, 0, 0, RTSYN_RUNTIME_CYCLE_STAGE_IMMEDIATE, &api, &host};
    const rtsyn_abi_runtime_context_t terminal_context{
        0, 0, 0, RTSYN_RUNTIME_CYCLE_STAGE_TERMINAL, &api, &host};

    ASSERT_EQ(descriptor->callbacks.create(&instance), RTSYN_ABI_STATUS_OK);
    ASSERT_NE(instance, nullptr);
    EXPECT_EQ(descriptor->callbacks.start(instance), RTSYN_ABI_STATUS_OK);
    EXPECT_EQ(descriptor->callbacks.process(instance, &immediate_context), RTSYN_ABI_STATUS_OK);
    EXPECT_EQ(descriptor->callbacks.process(instance, &terminal_context), RTSYN_ABI_STATUS_OK);
    EXPECT_EQ(descriptor->callbacks.stop(instance), RTSYN_ABI_STATUS_OK);
    descriptor->callbacks.destroy(instance);
}

TEST(ComediDevice, SkipsRuntimePortsWhenDisconnected) {
    const auto *descriptor = rtsyn_comedi_get_descriptor();
    void *instance = nullptr;
    HostContext host{};
    const rtsyn_abi_runtime_api_t api{read, write, is_connected};
    const rtsyn_abi_runtime_context_t immediate_context{
        0, 0, 0, RTSYN_RUNTIME_CYCLE_STAGE_IMMEDIATE, &api, &host};
    const rtsyn_abi_runtime_context_t terminal_context{
        0, 0, 0, RTSYN_RUNTIME_CYCLE_STAGE_TERMINAL, &api, &host};

    ASSERT_EQ(descriptor->callbacks.create(&instance), RTSYN_ABI_STATUS_OK);
    ASSERT_NE(instance, nullptr);
    EXPECT_EQ(descriptor->callbacks.process(instance, &immediate_context), RTSYN_ABI_STATUS_OK);
    EXPECT_EQ(descriptor->callbacks.process(instance, &terminal_context), RTSYN_ABI_STATUS_OK);
    for (uint32_t port = 0; port < descriptor->port_count; ++port) {
        EXPECT_FALSE(host.output_written[port]);
        EXPECT_EQ(host.read_count[port], 0U);
    }
    descriptor->callbacks.destroy(instance);
}

} // namespace
