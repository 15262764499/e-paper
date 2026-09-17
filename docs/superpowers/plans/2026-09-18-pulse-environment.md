# Pulse serial environment implementation

Goal: display live AHT20 temperature/humidity in desktop Pulse and record every
received serial byte; rebuild the Windows installer and matching firmware.

Architecture: retain Pulse v1 framing and 115200 8N1. Add unsolicited 0x90
environment frames with payload `<BiI` (AHT20 status, signed centi-C, unsigned
centi-percent). Reuse existing split-phase sensor acquisition in all LED modes.
Invalid readings clear the display; readings expire after five seconds.
Log raw RX before parsing, including malformed frames and boot text, as hex and
escaped text. Keep a bounded live UI history and an untruncated session log file.

- [x] Add failing device tests for fragmented telemetry, CRC, status, expiration,
  disconnect, and raw bytes; add a live-log UI regression.
- [x] Implement firmware emission, extend native tests, compile firmware.
- [x] Implement device decoding, disk log, dashboard and live connection records.
- [x] Run desktop/native regression tests and screenshot review.
- [x] Update protocol and usage docs, bump installer to 1.2.0, build installer,
  run packaged smoke test and verify distributed installer checksum.

Validation: 26 desktop tests pass; native UART and LED tests pass; ARM application
build succeeds. Packaged smoke test exits 0 with 13 real PC metric samples.
COM4 hardware HELLO returned valid ACKs (20 received bytes); the installed board
firmware did not report environment data. New MCU firmware has NOT been flashed.
Installer product version 1.2.0, size 33,918,952 bytes; desktop/Pulse-Setup.exe and
desktop/dist/Pulse-Setup.exe SHA256 both
`D6DD6607627B5E0EC1698442A88604E3652C119C988899E2D361096A4003D50F`.
