# Minimal AUTOSAR Adaptive Framework

This project implements a minimal version of the AUTOSAR Adaptive Platform's Execution Manager (EM) using C++17. It includes:

- Execution Manager with app supervision
- JSON-based manifests
- App lifecycle control (start-on-boot, on-failure restarts)
- Dummy app (`sensor_provider`) for demonstration

## Structure
- `em/`: Execution Manager source
- `apps/`: Adaptive apps
- `manifests/`: App configuration files
- `build/`: Build output (ignored)

## Build & Run
```bash
mkdir build && cd build
cmake ..
make
./execution_manager
