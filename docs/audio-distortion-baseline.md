# Synthetic audio distortion baseline

The deterministic TC128/64 test applies simple real-audio impairments before
the modem receiver. The voice-band model is a first-order 300 Hz high-pass
followed by a 3000 Hz low-pass. Echo delay is expressed at 48 kHz.

| Channel | Result before equalizer |
|---|---:|
| Identity | pass |
| 300–3000 Hz voice band | pass |
| 6-sample echo, gain 0.20 | pass |
| 12-sample echo, gain 0.30 | pass |
| 12-sample echo, gain 0.45 | pass |
| 20-sample echo, gain 0.40 | pass via equalizer fallback |
| Hard clipping at 70% peak | pass |
| Voice band + 8-sample echo 0.20 + clipping at 75% | pass |

The receiver estimates complex gain from the final 32-symbol training block and
normalizes recovered symbols before soft demapping. This makes LiquidDSP EVM
and LLR scale meaningful: ideal RMS EVM is about 0.105, while the failing strong
echo is now around 0.139 after carrier correction. The original failure was not
primarily equalizer-limited: adjacent-symbol CFO estimation interpreted ISI as
about -0.898 Hz of carrier offset, producing 521 hard-bit errors over the long
TC128 frame. An 8-symbol-lag differential estimator reduces the false estimate
to about +0.032 Hz and the hard-error count to zero. The fractionally-spaced
equalizer remains intended for more severe frequency-selective channels, not
for adaptive PHY negotiation.

The 20-sample, gain-0.40 echo is the equalizer gate: acquisition correlation
remains about 0.962 and CFO error is small, but the ordinary packet path leaves
73 hard-bit errors and fails FEC/CRC. The two-samples/symbol RLS retry reduces
this to 13 hard-bit errors, which TC128 corrects; CRC then passes. All milder
channels stay on the ordinary one-sample/symbol path.
