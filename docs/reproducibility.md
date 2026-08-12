# Reproducibility rules

Phase 0/1 tests are deterministic and run without audio hardware or network
access.

- Symbol and training vectors are committed as literal arrays.
- Pseudo-random channel noise uses a committed 32-bit LCG seed. A test must set
  its seed explicitly before each independent noise sweep point.
- Channel parameters belong in test tables, not hidden global defaults.
- Floating-point assertions use explicit tolerances and must not depend on wall
  clock time, host entropy, or test execution order.
- Generated WAV/vector artifacts are derived outputs. Only small vectors that
  expose a regression should be committed.
- Every real-channel failure should become a fixed WAV replay or a deterministic
  synthetic regression where practical.

The current AWGN smoke test specifies audio-sample standard deviation, not
Eb/N0. An Eb/N0-calibrated channel harness will be added before modem
performance claims are made.
