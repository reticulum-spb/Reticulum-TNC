# Third-party references and generated data

No third-party runtime FEC library is linked into this project.

## CCSDS TC LDPC

`tools/codes/ccsds_tc128.json` and `tools/codes/ccsds_tc512.json` describe the
circulant parity-check blocks for the CCSDS `(128,64)` and `(512,256)`
telecommand LDPC codes. The compact representations and encoder oracle vectors
were cross-checked against
[adamgreig/labrador-ldpc](https://github.com/adamgreig/labrador-ldpc), copyright
2017 Adam Greig, licensed under the MIT License.

The generated headers contain expanded adjacency and systematic parity masks
produced by this project's generator. No Rust code is linked or required to
regenerate committed tables.

Labrador-LDPC's MIT license is available in its upstream
[LICENSE file](https://github.com/adamgreig/labrador-ldpc/blob/master/LICENSE).
Retain this attribution if the CCSDS input description or oracle-derived tests
are redistributed.
