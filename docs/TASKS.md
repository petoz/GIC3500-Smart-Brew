# Development Tasks & Roadmap

## Phase 1: Core Functionality (Completed)
- [x] Initial ESP-IDF project structure.
- [x] Integrate CD74HC4067 multiplexer logic for GIC3500.
- [x] Establish Wi-Fi Provisioning AP flow (`GIC3500-Config`).
- [x] Basic web interface with simple Start/Stop and JSON scheduling.
- [x] PID control loop targeting temperature holds without overshoot.
- [x] MQTT connection handling and status publishing (`brew/cooker/status`).

## Phase 2: Security & Cleanup (Completed)
- [x] Remove hardcoded Wi-Fi strings and MQTT credentials from source code and Git history.
- [x] Save all user networking configurations into the internal NVS storage over the web API.
- [x] Enable HTTP Web Server persistence upon reboot.

## Phase 3: Advanced Controls and OTA (Completed)
- [x] Configurable Mashing Steps (1-5 dynamic steps) with NVS storage survival.
- [x] Manual Override Mode (0-11 Stages) via the Web UI to bypass PID logic.
- [x] MQTT System Logging feature switch for remote headless debugging (`brew/cooker/log`).
- [x] Over-The-Air (OTA) firmware update capabilities fully available in the web interface.

## Future Ideas / Backlog
- [ ] Implement recipe selection (saving multiple named schedules in NVS).
- [ ] Interactive chart plotting current temperature vs target temperature using Plotly.js or Chart.js on the web UI.
- [ ] Native iOS/Android companion app leveraging zero-configuration networking (mDNS).
