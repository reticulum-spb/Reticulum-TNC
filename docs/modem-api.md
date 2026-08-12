# Modem API

The public modem API is a caller-owned C interface built around
`rtnc_modem_t`. It does not allocate per frame. The caller owns the modem,
audio buffers, decoded payload buffer, and `rtnc_modem_workspace_t` used by
the full decoder.

## Profile initialization

Use `rtnc_modem_init_profile()` for an ordinary DATA modem. It is equivalent
to selecting `RTNC_PREAMBLE_DATA` explicitly:

```c
rtnc_modem_t modem;

if (!rtnc_modem_init_profile(
        &modem, fec_mode, payload_class_bytes, &phy_profile)) {
    /* Invalid profile or DSP initialization failure. */
}
```

Use `rtnc_modem_init_profile_preamble()` when the preamble namespace matters:

```c
rtnc_modem_t data_modem;
rtnc_modem_t control_modem;

rtnc_modem_init_profile_preamble(
    &data_modem, data_fec, data_payload_class, &data_phy,
    RTNC_PREAMBLE_DATA);
rtnc_modem_init_profile_preamble(
    &control_modem, control_fec, control_payload_class, &control_phy,
    RTNC_PREAMBLE_CONTROL);
```

DATA and CONTROL use distinct low-cross-correlation acquisition/training
sequences. This distinction is required even when both channels use BPSK, or
when they use the same modulation with different symbol rates. It is a frame
type discriminator, not adaptive modulation or profile negotiation.

Call `rtnc_modem_deinit()` once for every successfully initialized modem. A
modem and its LiquidDSP state must not be used concurrently by multiple
threads without caller-provided synchronization.

## Transmit

`rtnc_modem_tx_audio()` accepts the initialized modem to use for this frame:

```c
rtnc_modem_t *tx_modem = send_control ? &control_modem : &data_modem;

status = rtnc_modem_tx_audio(
    tx_modem, payload, payload_length,
    audio, audio_capacity, &audio_samples);
```

There is no global transmit-profile switch. Passing the modem pointer makes
transmission through any number of preinitialized profiles possible. The
runtime may listen to at most two profiles while still retaining more modem
objects solely for transmission.

The payload length must not exceed the modem's 64- or 128-byte physical
payload class. Fragmentation of larger Reticulum packets is performed above
this API.

## Receive one profile

For a complete captured candidate, call `rtnc_modem_rx_audio()` with a
caller-owned workspace:

```c
rtnc_modem_workspace_t workspace;
rtnc_sync_metrics_t metrics = {0};

status = rtnc_modem_rx_audio(
    &data_modem, candidate, candidate_samples,
    payload, sizeof(payload), &payload_length,
    &metrics, &workspace);
```

`rtnc_modem_rx_audio_fast()` runs only the ordinary one-sample-per-symbol
path. A runtime may enqueue a rejected candidate for a lower-priority full
decode with equalization. Keep a separate workspace for every decoder thread.

On success, `metrics` reports acquisition, timing, CFO, phase, EVM, effective
training SNR, and equalizer diagnostics. A detected frame that fails FEC or
CRC returns `RTNC_MODEM_FRAME_REJECTED`; absence of a usable frame is distinct
from invalid arguments and DSP failures.

## Receive two profiles simultaneously

Two-profile reception uses two ordinary modem objects and two ordinary
acquisition detectors. There is deliberately no multi-detector container:

```c
rtnc_acquisition_detector_t control_detector;
rtnc_acquisition_detector_t data_detector;

rtnc_acquisition_detector_init_modem(
    &control_detector, &control_modem, 4U);
rtnc_acquisition_detector_init_modem(
    &data_detector, &data_modem, 4U);
```

Feed every audio sample to `rtnc_acquisition_detector_process_two()`:

```c
rtnc_modem_t *detected = NULL;
float control_score;
float data_score;

bool trigger = rtnc_acquisition_detector_process_two(
    &control_detector,
    data_enabled ? &data_detector : NULL,
    sample,
    &detected,
    &control_score,
    &data_score);

if (trigger && detected == &control_modem) {
    capture_for_control_decoder(sample);
} else if (trigger && detected == &data_modem) {
    capture_for_data_decoder(sample);
}
```

The second detector may be `NULL`, which removes its per-sample DSP cost.
When either detector crosses its configured normalized threshold, the
function returns the exact `rtnc_modem_t *` retained during detector
initialization. If both trigger on the same sample, the larger margin above
its own threshold wins.

Each selected path needs independent candidate capture, decode queue,
workspace, and decoder state. Do not decode one candidate through both
profiles, and do not synchronously drain a DATA decode queue in the audio
capture thread: doing so can stall capture and lose a CONTROL announcement.
The OTA benchmark in `tools/ota_benchmark.c` is the current reference for this
arrangement.

Listening to more than two profiles is intentionally outside this API. It
would multiply continuous matched-filter/correlation cost. A runtime that
needs a different DATA profile should keep CONTROL active, initialize or
replace the second modem after receiving control information, and then resume
the second detector.

## Rate and buffer sizing

`rtnc_modem_profile_rate()` calculates raw, post-FEC, and conservative
interface bitrates without constructing LiquidDSP objects.
`rtnc_modem_frame_samples()` returns the audio sample count required for a
maximum-payload frame of an initialized modem. Fixed maximum sizes are exposed
as `RTNC_MODEM_MAX_AUDIO_SAMPLES`, `RTNC_MODEM_MAX_LLR`, and related constants
in `include/rtnc/modem.h`.
