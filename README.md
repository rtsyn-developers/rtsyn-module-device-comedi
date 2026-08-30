# RTSyn Comedi Device

COMEDI data acquisition device for analog and digital hardware I/O.

This template implements a generic RTSyn device module. Device modules are called by the runtime at
the beginning and at the end of each cycle, so generated process code is split between immediate and
terminal stage hooks.

## Usage

### Update

Make sure you have last version of the dependencies:

```bash
xrepo update-repo
xmake require --upgrade
```

For development you may need to run:

```bash
xmake require --upgrade -fy <dependency_name>
```

### Compiling

```bash
xmake
```

### Tests

```bash
xmake test
```

For enabling valgrind, before running tests:

```bash
xmake f --valgrind=y
```

### Local development

If you want to test your changes locally from different parts of RTSyn, export the path where you have all the repos:

```bash
export RTSYN_WORKSPACE=<PATH>
```

> [!WARNING]
> This expects you also the `rtsyn-xmake-repo`.

## Hardware channels and acquisition

The descriptor discovers `/dev/comedi0` when loaded. For a PCI-6024E it
exposes 2 analog outputs, 16 analog inputs, and both directions of the
8 general-purpose DIO lines. Each DIO line is shared: use a line in only
one direction at a time. Other Comedi subdevices (PFI, counters, calibration,
memory and serial) are not exposed as general-purpose DIO channels.

Analog ports carry `f64` volts. Digital ports carry integer 0 or 1.
The GUI Inputs section feeds hardware outputs; its Outputs section reports
hardware inputs. Graph-connected or telemetry-subscribed source channels
are acquired; removing the final subscription stops an unconnected channel.
Add AI0 to Runtime telemetry to open it without connecting it to a neuron.
Unused channels are not sampled merely because the device card is open.

`analog_output_gain_N` multiplies a signal before the DAC, and
`analog_input_gain_N` multiplies the acquired voltage before publishing it.
Both default to 1.0 and accept finite floating-point values. Existing output
gain parameter IDs are preserved; input gains follow the output gains.
For a millivolt neuron waveform, output gain 1000 and input gain 0.001
provide a volt-scale physical loopback and restore the software voltage scale.
Software gain does not increase hardware converter resolution.

Descriptor IPC now supports 64 ports and 65 parameters. Rebuild the launcher,
engine, API and SPSC library together and restart the daemon after updating;
old processes must not share queues with the new message layout. Reload the
Comedi module to refresh its discovered ports and parameter controls.

### Analog offsets

Each analog channel exposes an `analog_output_offset_N` or
`analog_input_offset_N` f64 parameter, defaulting to 0.0. Both directions apply
`value * gain + offset`. Output offsets are physical volts, before hardware
range clamping; input offsets use the scaled input units. For example, with
AI gain 0.001, a physical +10 mV bias is corrected with offset -0.00001.
Offsets are appended after all gains, preserving existing gain IDs. On the
PCI-6024E, AO offsets are IDs 19–20 and AI offsets are IDs 21–36. The GUI
provides editable floating-point controls. Digital channels remain binary.
