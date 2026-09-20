# Changelog

## 0.2.3 — EchoScope

- Rename the application and setup network to EchoScope; retain existing settings storage.
- Add a Makefile with local tool setup, dependency installation, builds, host tests, image packaging, app-only/full flashing, serial monitoring and factory backup.
- Add GitHub Actions build/test workflow and firmware artifacts.
- Document supported hardware, controls, setup, updates, limitations and the planned printable enclosure.

## 0.2.2

- Highlight the selected aircraft trail in amber above other paths.
- Add east/south/west compass labels.

## 0.2.1

- Use two quadrature transitions per detent for one action per physical click.

## 0.2.0

- Toggle rotation between zoom and aircraft selection with a bare knob press.
- Indicate the active mode in the footer; coordinate touch and mechanical clicks.
- Retain faint aircraft paths while they remain visible, storing bounded histories in PSRAM.

## 0.1.3

- Separate network and display tasks onto different CPU cores and limit redraw frequency, resolving the observed TLS stalls on the device.

## 0.1.0–0.1.2

- Initial radar, touch details, Wi-Fi/location setup, live ADS-B data and demo mode.
- Add certificate-chain support and connection diagnostics.
