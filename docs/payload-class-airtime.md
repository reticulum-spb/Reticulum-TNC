# Payload class airtime

The fixed profile determines whether a radio frame can carry up to 64 or 128
payload bytes. The table below uses a full-class payload and includes the
current training, one-byte header, CRC, FEC padding, RRC delay/flush, and all
audio samples emitted by the offline modem. It excludes PTT lead/tail silence.

| FEC mode | Payload bytes | Audio samples | Airtime at 48 kHz | Noiseless ceiling payload B/s |
|---|---:|---:|---:|---:|
| NONE | 64 | 15,200 | 0.3167 s | 202.105 |
| NONE | 128 | 25,440 | 0.5300 s | 241.509 |
| TC128 / ROBUST | 64 | 27,520 | 0.5733 s | 111.628 |
| TC128 / ROBUST | 128 | 48,000 | 1.0000 s | 128.000 |
| TC512 / NORMAL baseline | 64 | 35,200 | 0.7333 s | 87.273 |
| TC512 / NORMAL baseline | 128 | 55,680 | 1.1600 s | 110.345 |

The 128-byte class has the higher ceiling in every current mode because it
amortizes fixed acquisition and integrity overhead. This does not select it as
the better near-threshold class: a longer frame can have sufficiently worse FER
to erase the ceiling advantage. A calibrated equal-channel payload-class FER
sweep remains required.
