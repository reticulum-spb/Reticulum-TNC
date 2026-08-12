# Phase 1 experimental frame

This byte frame exists only to exercise the offline QPSK modem. It is not a
frozen radio protocol and may change together with its tests.

The current FEC mode is `FEC_NONE`. PHY profile and protocol version are fixed
by configuration and are therefore not repeated in every frame.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | payload length minus one (`0..127` means `1..128` bytes) |
| 1 | variable | payload |
| end-2 | 2 | CRC-16/CCITT-FALSE over header and payload |

The parser rejects inconsistent lengths, truncated/trailing data, insufficient
output space, and CRC errors. Zero-length radio payloads are not encoded.

This follows the one-byte-header idea used by FreeDV-TNC, adjusted for the
128-byte maximum payload: using `length - 1` allows all lengths 1 through 128
to fit in one byte. Fragment metadata is intentionally absent until Phase 4.
Combining a 128-value length, fragment type, and fragment ID in one byte is not
possible without restricting one of those fields, so that design remains open.

## Coded-length acquisition

For `FEC_LDPC_ROBUST`, the configured payload class fixes the number of TC128
codewords before reception: class 64 protects a 67-byte maximum frame using
9 codewords, while class 128 protects a 131-byte maximum frame using 17
codewords. The actual one-byte length remains inside FEC and CRC protection.
Unused protected bytes and the final partial 8-byte LDPC information block are
zero-padded. There is no rate negotiation or unprotected length field.

## Acquisition sequence

Coarse acquisition uses the following 32-symbol QPSK vector:

    0 3 1 1 2 0 3 2 1 0 2 3 3 0 1 2
    2 1 3 0 1 3 2 2 0 1 0 3 2 3 1 0

It is followed by 64 deterministic active QPSK symbols generated from a fixed
xorshift sequence. The first 32 give the LiquidDSP RRC timing loop room to
converge; the final 32 form a separate timing/phase training sequence. Total
acquisition and training overhead is therefore currently 96 symbols.

Coarse frame start and CFO are estimated on the first vector by normalized
complex correlation at the configured 40 samples/symbol. After sample-domain
CFO correction, `symsync_crcf` produces the symbol stream and the final
training sequence locates its payload boundary. This is still an experimental,
unfrozen preamble.
