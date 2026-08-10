# W6300 official network test

The Arduino `Ethernet.h` build hangs on W6300-EVB-PICO2 because that library probes W5100/W5200/W5500 over normal SPI. W6300-EVB-PICO2 uses WIZnet's PIO/QSPI driver.

This directory contains a UF2 built from the official `WIZnet-PICO-C` HTTP server example, configured for:

- IP: `192.168.88.188`
- Subnet: `255.255.255.0`
- Gateway: `192.168.88.254`
- DNS: `1.1.1.1`
- HTTP port: `80`
- Board: `W6300_EVB_PICO2`
- QSPI mode: `QSPI_QUAD_MODE`

## Source

The matching source code has been copied into `w6300_official_test/`:

- `w6300_official_test/wizchip_http_server.c`
- `w6300_official_test/web_page.h`
- `w6300_official_test/CMakeLists.txt`
- `w6300_official_test/UPSTREAM_README.md`

These files are the official WIZnet HTTP server example with the network settings changed to `192.168.88.188`.

## Flash

1. Hold `BOOTSEL`.
2. Plug USB into the W6300-EVB-PICO2.
3. Release `BOOTSEL`.
4. Copy `w6300_official_http_test_192.168.88.188.uf2` to the mounted `RP2350` drive.

Or use `picotool` after the board is in BOOTSEL mode.

## Test

After reboot, wait a few seconds and test:

```bash
ping 192.168.88.188
curl http://192.168.88.188/
```

If this works, the hardware, cable, switch and static IP are good. The production relay webserver is in `wiznet/src/relay_server.cpp`.
