# Fractionally-spaced equalizer

The isolated equalizer wrapper uses LiquidDSP `eqrls_cccf` with 17 complex taps,
two input samples per symbol, supervised training, fixed caller-owned state, and
tap/error export for diagnostics. Modulation, baud rate, and FEC remain fixed;
this is channel equalization, not adaptive PHY negotiation.

LiquidDSP 1.6 marks its legacy `eqlms_cccf` type deprecated because of an
upstream header attribute around the old batch-training API. The non-deprecated
RLS object is used instead. A forgetting factor of 0.99 converges on the
deterministic fractional ISI test from roughly symbol-scale error to mean error
below 0.00002 during both the end of training and the held-out test interval.

The real-audio isolation test now feeds the equalizer from `symsync` at two
samples/symbol after RRC TX, 1650 Hz up/down conversion, and the exact TC128/64
codeword sequence. On the new 20-sample, gain-0.40 echo gate, training on the
final 32 stable symbols with forgetting factor 0.95 reduces held-out mean vector
error from about 0.317 to 0.101 and hard-symbol errors from one to zero.

The former TC128/64 strong-echo packet failure was subsequently traced to an
ISI-biased adjacent-symbol CFO estimator and now passes without equalization.
Packet RX now invokes this path only after an ordinary coded-frame FEC failure.
It replays the caller-owned audio buffer through `symsync` at two samples per
symbol, trains RLS on the final 32 known symbols, retries soft FEC/CRC, exports
taps/training error, and restores `symsync` to one sample per symbol before
returning. The 20-sample/0.40 echo improves from 73 to 13 hard-bit errors and
passes TC128 plus CRC. Unconditional equalization remains intentionally absent.

A centered-tap variant with a four-symbol decision delay was also measured.
Using either 28 final-training updates or 60 late known-symbol updates produced
mean vector error around 0.126–0.134, no better than the unequalized 0.126 and
worse than the causal 0.106 result. The precursor/decision-delay hypothesis was
therefore rejected and the additional delay was not retained.
