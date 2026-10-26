# Chapter 9 firmware

`rustsbi-qemu.bin` is the RustSBI 0.3.1 / RustSBI-QEMU 0.2.0-alpha.2
firmware distributed with `rcore-os/rCore-Tutorial-v3` at commit
`209476f27359b47cd6534ebb91afc5d01692b429`.

SHA-256: `31f5d013f81421ed9fd33208dfa1c3796f9d72bd11c0f939566581ac2f3e97d3`.

Source project: <https://github.com/rustsbi/rustsbi-qemu>.
This chapter uses its RISC-V SBI timer and shutdown services. UART input and
output are handled directly by the supplied supervisor-mode UART driver.
