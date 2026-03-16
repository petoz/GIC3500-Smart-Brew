# Changelog

All notable changes to this project will be documented in this file.

## [1.1.0] - 2026-03-16
### Added
- **Dynamic Mashing Schedule:** Added UI and backend support for 1-5 variable mashing steps.
- **OTA Updates:** Full support for Over-The-Air firmware flashing via the web UI built-in uploader.
- **NVS Persistence for Schedule:** The configured times and temperatures are now securely saved to flash and persist across unplugs/reboots.
- **Manual Control Mode:** Added a manual override slider in the Web UI to explicitly bypass the PID and enforce a specific power stage level (1-11) for instances like hop boiling or manual mashing.
- **MQTT Logging Intercept:** Integrated a `vprintf` hook that catches internal system logs and publishes them to the `brew/cooker/log` topic, toggleable via the Web UI.

### Changed
- Refactored `CMakeLists.txt` structures to append `app_update` dependencies for OTA.
- Restructured `main.c` state machine to seamlessly bounce between native AUTO (PID) control and MANUAL staging limits.

## [1.0.1] - 2026-03-16
### Security
- Completely expunged hardcoded Wi-Fi credentials and sensitive MQTT passwords from previously committed code out of the active Git history to ensure secure public deployment.

## [1.0.0] - Initial Release
### Added
- Core PID temperature controller loop logic.
- Basic CD74HC4067 multiplexing driver implementation for the GIC3500 cooker.
- Local Web Server built into the C backend with a single page application (SPA) frontend.
- MQTT connectivity to read ambient sensor fluid data (`brew/sensor/temp`) and broadcast running statuses.
