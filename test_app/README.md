# ESP32 Test App

A lightweight diagnostic web application for verifying ESP32 devices over both local network (LAN) and Bluetooth Low Energy (BLE).

## Features

- **Scan Network** – Pings the active IPv4 subnets, parses the ARP table, and lists devices whose MAC address matches known Espressif (ESP32) prefixes. Displays `None` when nothing is discovered.
- **BLE Connect** – Uses the browser-hosted Web Bluetooth API to connect to ESP32 boards that advertise with a name starting with `ESP32`, reporting connection status.
- Simple, self-contained Express server that serves the static front-end and exposes `/api/scan`.

## Prerequisites

- Node.js 16 or newer (the repository machine currently uses Node 16.16.0).
- Access to run `arp` and ICMP echo (ping) commands locally.
- A Chromium-based browser with Web Bluetooth support (Chrome, Edge, or Brave). Web Bluetooth requires HTTPS or `http://localhost`.

## Setup

```bash
cd "testing/test app"
npm install
```

## Run

```bash
npm start
```

Then open `http://localhost:5173` in a supported browser.

## Usage Notes

- The network scan may take several seconds while the app pings each host on the detected /24 subnets. Devices that have not communicated recently might not appear until they respond to a ping.
- Espressif releases multiple MAC address ranges; the server checks against the most common prefixes. If your board uses an uncommon OUI, add it to `ESPRESSIF_OUIS` in `server.js`.
- Web Bluetooth prompts the user to select a device. A user gesture (button click) is required, and the feature is not available in all browsers.
- No sensitive data is stored; the app only inspects devices reachable on your LAN.

## Troubleshooting

- **No devices found on the network scan**: Ensure the ESP32 is powered, connected to the same subnet, and has recently exchanged traffic. You can also manually ping its IP to populate the ARP cache before scanning.
- **BLE connection fails immediately**: Confirm the ESP32 is advertising over BLE and that your browser/computer has Bluetooth hardware enabled.
