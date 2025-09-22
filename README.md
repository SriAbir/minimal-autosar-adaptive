# Minimal AUTOSAR Adaptive Framework

This project implements a minimal version of the AUTOSAR Adaptive Platform using C++17. It includes:

- Execution Manager with app supervision
- JSON-based manifests
- App lifecycle control (start-on-boot, on-failure restarts)
- Dummy app (`sensor_provider`) for demonstration
- SOME/IP(+SD) binding shim with Provider & Consumer Apps for testing
- And more...


## Structure
- `apps/`: Some example apps (Adaptive, Provider, Consumer apps)
- `ara/com/`: Adapter to someip
- `build/`: Build output (ignored)
- `com/`: SOME/IP(+SD) binding shim source
- `em/`: Execution Manager source
- `include/ara/`: Apps' interface to ara 
- `logging/`: Logging implemenentation
- `manifests/`: App configuration files
- `persistency/`: Persistency (can store files or key-value pairs)
- `phm/`: Health management
- `services/`: Central description of offered services
- `tests/`: What it sounds like. Might not have full coverage...
- `vsomeip/`: Not from this repo. Follow instructions to pull. NOTE: You need to add local.json file here.

## Getting vsomeip
GETTING pkg-config:
```bash
sudo apt-get update
sudo apt-get install -y pkg-config
```
GETTING zlib
```bash
sudo apt-get install -y zlib1g-dev
rm -rf build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j"$(nproc)"
sudo make install
sudo ldconfig
Verify pkg-config can see it: pkg-config --cflags --libs vsomeip3
```
GETTING someIP:
```bash
sudo apt-get install -y build-essential cmake pkg-config \
  libboost-system-dev libboost-thread-dev libboost-log-dev \
  libboost-program-options-dev libboost-filesystem-dev \
  libboost-chrono-dev libboost-date-time-dev libboost-regex-dev \
  libboost-random-dev libcap-dev libsystemd-dev
git clone https://github.com/COVESA/vsomeip.git
cd vsomeip && mkdir build && cd build
cmake ..
make -j
sudo make install
sudo ldconfig
```
Add local.json under `vsomeip`. Example:
```json
{
  "unicast": "127.0.0.1",
  "logging": { "level": "debug", "console": "true" },
  "applications": [
    { "name": "sensor_provider", "id": "0x4444" },
    { "name": "speed_client",    "id": "0x5555" },
    { "name": "phm_supervisor",  "id": "0x7a01" }
  ],
  "routing": "sensor_provider",
  "service-discovery": { "enable": true }
}
```
If you haven't placed your local.json under `vsomeip/` in your project you will need to run the following command when in `your_project/build/` before starting the Execution Manager:
```bash
export VSOMEIP_CONFIGURATION=/absolute/path/to/vsomeip/local.json
```
## Build & Run
```bash
rm -rf build
cmake -S . -B build
cmake --build build -j
cd build
./execution_manager (runs example apps speed_provider and speed_client)
./someip_provider.cpp
./service_consumer.cpp
```
## Steps to add your own app

### 1. Edit services/services_description.hpp and declare your service IDs and the codec for payloads you use. Example:
```cpp
struct SpeedDesc {
  static constexpr uint16_t Service  = 0x1234;
  static constexpr uint16_t Instance = 0x5678;

  // Event group & event (for notifications)
  struct SpeedEvent {
    static constexpr uint16_t Id         = 0x0420;
    static constexpr uint16_t EventGroup = 0x0001;
    using Payload = float; // serialized with ara::com::Codec<float>
  };
};
```
### 2. Add a file <your_app>.cpp in `apps/`. Some templates are provided below.
#### A. Provider template:
```cpp
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <thread>
#include <string>

#include "log.hpp"
#include "sinks_console.hpp"

#include "ara/com/core.hpp"
#include "ara/com/someip_adapter.hpp"
#include "services_description.hpp"
#include <ara/phm/supervision_client.hpp>

using namespace std::chrono_literals;

static std::atomic<bool> running{true};
static void on_sig(int){ running = false; }

int main(){
  std::signal(SIGINT, on_sig);
  std::signal(SIGTERM, on_sig);

  // Logging
  auto &LM = ara::log::LogManager::Instance();
  LM.SetGlobalIds("ECU1","sensor_provider");
  LM.SetDefaultLevel(ara::log::LogLevel::kInfo);
  LM.AddSink(std::make_shared<ara::log::ConsoleSink>());
  auto lg = ara::log::Logger::CreateLogger("SNS");

  // PHM client
  ara::phm::SupervisionClient phm("sensor_provider");
  phm.Connect();

  // ara::com runtime (backed by SOME/IP)
  ara::com::Runtime rt(ara::com::GetSomeipAdapter());

  // Offer service & event
  ara::com::Skeleton<SpeedDesc> skel(rt, "sensor_provider");
  skel.Offer();

  float t = 0.0f;
  while (running.load(std::memory_order_relaxed)) {
    phm.ReportAlive();

    float speed = 50.0f + 10.0f * std::sin(t);
    t += 0.2f;

    auto ec = skel.Notify<SpeedDesc::SpeedEvent>(speed);
    if (ec != ara::com::Errc::kOk)
      ARA_LOGWARN(lg, "Notify failed: {}", static_cast<int>(ec));

    std::this_thread::sleep_for(100ms);
  }

  skel.Stop();
  rt.adapter().shutdown();
  ARA_LOGINFO(lg, "Shutdown");
  return 0;
}
```
#### B. Subscriber template:
```cpp
#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>
#include <string>

#include "log.hpp"
#include "sinks_console.hpp"

#include "ara/com/core.hpp"
#include "ara/com/someip_adapter.hpp"
#include "services_description.hpp"

#include <ara/phm/supervision_client.hpp>
#include "persistency/key_value_storage_backend.hpp"
#include "ara/per/key_value_storage.hpp"

using namespace std::chrono_literals;

static std::atomic<bool> running{true};
static void on_sig(int){ running=false; }

int main(){
  std::signal(SIGINT, on_sig);
  std::signal(SIGTERM, on_sig);

  // Logging
  auto &LM = ara::log::LogManager::Instance();
  LM.SetGlobalIds("ECU1","speed_client");
  LM.SetDefaultLevel(ara::log::LogLevel::kInfo);
  LM.AddSink(std::make_shared<ara::log::ConsoleSink>());
  auto lg = ara::log::Logger::CreateLogger("SPD");

  // Persistency (simple KV)
  auto backend = std::make_shared<::persistency::KeyValueStorageBackend>("/var/adaptive/per/demo");
  ara::per::KeyValueStorage kv(backend);
  float max_speed = 90.0f;
  if (auto r = kv.GetValue<float>("max_allowed_speed"); r.HasValue()) max_speed = r.Value();
  else kv.SetValue("max_allowed_speed", std::to_string(max_speed));

  // PHM client
  ara::phm::SupervisionClient phm("speed_client");
  phm.Connect();

  // ara::com runtime
  ara::com::Runtime rt(ara::com::GetSomeipAdapter());

  // Proxy to provider
  ara::com::Proxy<SpeedDesc> proxy(rt, "speed_client");
  proxy.RequestService();

  std::atomic<int> missed{0};
  auto sub = proxy.Subscribe<SpeedDesc::SpeedEvent>([&](float speed){
    kv.SetValue("last_speed", std::to_string(speed));
    if (speed > max_speed)
      ARA_LOGWARN(lg, "Speed {} exceeds threshold {}", speed, max_speed);
    else
      ARA_LOGINFO(lg, "Speed={}", speed);
    missed.store(0, std::memory_order_relaxed);
  });

  while (running.load(std::memory_order_relaxed)) {
    phm.ReportAlive();
    if (missed.fetch_add(1, std::memory_order_relaxed) > 30) { // ~3s (100 ms period)
      ARA_LOGERROR(lg, "Missed speed events for >3s -> checkpoint");
      phm.ReportCheckpoint(0x1001);
      missed.store(0, std::memory_order_relaxed);
    }
    std::this_thread::sleep_for(100ms);
  }

  rt.adapter().unsubscribe_event(sub);
  proxy.ReleaseService();
  rt.adapter().shutdown();
  ARA_LOGINFO(lg, "Shutdown");
  return 0;
}
```
#### C. Provider and Subscriber template
```cpp
#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>
#include "log.hpp"
#include "sinks_console.hpp"
#include <ara/phm/supervision_client.hpp>

#include "ara/com/core.hpp"
#include "ara/com/someip_adapter.hpp"
#include "services_description.hpp"

using namespace std::chrono_literals;

static std::atomic<bool> running{true};
static void on_sig(int){ running = false; }

int main() {
  std::signal(SIGINT, on_sig);
  std::signal(SIGTERM, on_sig);

  auto &LM = ara::log::LogManager::Instance();
  LM.SetGlobalIds("ECU1","temp_bridge");
  LM.SetDefaultLevel(ara::log::LogLevel::kInfo);
  LM.AddSink(std::make_shared<ara::log::ConsoleSink>());
  auto lg = ara::log::Logger::CreateLogger("TMP");

  ara::phm::SupervisionClient phm("temp_bridge");
  phm.Connect();

  ara::com::Runtime rt(ara::com::GetSomeipAdapter());

  // Subscribe to upstream event
  ara::com::Proxy<UpstreamDesc> upstream(rt, "temp_bridge");
  upstream.RequestService();

  // Offer downstream service
  ara::com::Skeleton<DownstreamDesc> downstream(rt, "temp_bridge");
  downstream.Offer();

  auto sub = upstream.Subscribe<UpstreamDesc::InEvent>([&](float v){
    ARA_LOGINFO(lg, "InEvent={}", v);
    // forward / transform:
    auto ec = downstream.Notify<DownstreamDesc::OutEvent>(v * 2.0f);
    if (ec != ara::com::Errc::kOk) ARA_LOGWARN(lg, "Notify failed {}", (int)ec);
  });

  while (running) { phm.ReportAlive(); std::this_thread::sleep_for(200ms); }

  rt.adapter().unsubscribe_event(sub);
  downstream.Stop();
  upstream.ReleaseService();
  rt.adapter().shutdown();
  ARA_LOGINFO(lg, "Bridge shutdown");
  return 0;
}
```
#### D. Diagnostics
If your app needs to expose diagnostics (via a simple UDS-over-TCP shim), follow these steps:
1. Add adapter code to your app `<your_app>.cpp`:
```cpp
//Add include among the others

#include "ara/diag/diag_server.hpp"
#include <thread>
#include <cstdlib>

// Your neutral hooks (or real logic)
bool        function_hasTrigger()   { return false; }
const char* function_triggerCause() { return "None"; }
void        function_resetTrigger() { /* no-op */ }

static void run_diag_thread() {
  ara::diag::DiagServer srv;

  // Register RDBI (0x22) and RoutineControl (0x31) handlers.
  // Map to your *private* DID/RID at runtime (placeholder below).
  const uint16_t DID_HasTrigger   = 0x1234;  // example only
  const uint16_t DID_TriggerCause = 0x1235;  // example only
  const uint16_t RID_ResetTrigger = 0x1000;  // example only

  srv.RegisterRdbi({
    .onRead = [&](uint16_t did){
      using ara::diag::Nrc;
      if (did == DID_HasTrigger) {
        uint8_t v = function_hasTrigger() ? 1 : 0;
        return std::pair{Nrc::kOk, std::vector<uint8_t>{v}};
      }
      if (did == DID_TriggerCause) {
        std::string s = function_triggerCause();
        return std::pair{Nrc::kOk, std::vector<uint8_t>(s.begin(), s.end())};
      }
      return std::pair{Nrc::kOutOfRange, std::vector<uint8_t>{}};
    }
  });

  srv.RegisterRoutine({
    .onRoutine = [&](uint8_t sub, uint16_t rid, const std::vector<uint8_t>&){
      using ara::diag::Nrc;
      if (sub == 0x01 && rid == RID_ResetTrigger) { function_resetTrigger(); return Nrc::kOk; }
      return Nrc::kSubFuncNotSupported;
    }
  });

  const char* bind = std::getenv("DIAG_BIND") ? std::getenv("DIAG_BIND") : "127.0.0.1";
  uint16_t port = std::getenv("DIAG_PORT") ? static_cast<uint16_t>(std::stoi(std::getenv("DIAG_PORT"))) : 0;

  // Starts the tiny UDS-over-TCP loop internally (DoIP-like)
  srv.Run(bind, port);   // blocks; call in its own thread
}

int main() {
  std::thread diag_thr(run_diag_thread);

  // ... your normal app code ...

  diag_thr.join();
}
```
Replace the hook implementations with real logic when you want diagnostics to reflect your app’s state.

2. Handle multiple apps
If you run multiple apps with diagnostics enabled, give each a unique port.
You can set this at runtime:
```bash
export DIAG_PORT=13401
./apps/my_app
```
By default, the server binds to 127.0.0.1:13400. Using 127.0.0.1 is safe: it only accepts local connections.
3. Config and safety
- Public repo: commit only diagnostics/config/diag-example.json with placeholder IDs.
- Private repo: keep your real DID/RID mappings in diagnostics/config/diag.json (add to .gitignore).
- Example environment setup:
```bash
export DIAG_CONFIG_PATH=diagnostics/config/diag-example.json
```
4. Testing with an UDS client
Send a ReadDataByIdentifier request (0x22) [Python]:
```python
import socket, struct
s = socket.socket(); s.connect(("127.0.0.1",13400))
req = bytes([0x22,0x12,0x34])           # DID 0x1234 (example)
s.sendall(struct.pack(">H",len(req))+req)
rlen = struct.unpack(">H",s.recv(2))[0]
print("UDS rsp:", list(s.recv(rlen)))
```

### 3. Add a manifest file <your_app_name>.json in `manifests/`. Some templates are provided below.
Executable path: use an absolute path or install step so EM’s `execl()` finds it reliably.

#### A. Provider template:

```json
{
  "app_id": "temp_provider",
  "executable": "/absolute/path/to/build/temp_provider",
  "start_on_boot": true,
  "restart_policy": "on-failure",
  "dependencies": ["others_if_any"],

  "phm": {
    "period_ms": 1000,
    "allowed_missed_cycles": 3,
    "required_checkpoints": ["alive"]   // optional; leave empty or remove if not used
  },

  "com": {
    "someip": {
      "service_id":  "0x1234",
      "instance_id": "0x0001",
      "event_group": "0x0001",
      "subscribe": []                   // provider doesn’t subscribe
    }
  }
}
```
#### B. Subscriber template:
```json
{
  "app_id": "temp_subscriber",
  "executable": "/absolute/path/to/build/temp_subscriber",
  "start_on_boot": true,
  "restart_policy": "on-failure",
  "dependencies": ["temp_provider"],

  "phm": {
    "period_ms": 1000,
    "allowed_missed_cycles": 3,
    "required_checkpoints": ["alive"]
  },

  "com": {
    "someip": {
      "service_id":  "0x1234",
      "instance_id": "0x0001",
      "event_group": "0x0001",
      "subscribe":   ["0x100"]         // list events you want; EM builds SOMEIP_REQUEST_EVENTS
    }
  }
}
```
#### C. Provider and subscriber template:
```json
{
  "app_id": "temp_bridge",
  "executable": "/absolute/path/to/build/temp_bridge",
  "start_on_boot": true,
  "restart_policy": "on-failure",

  "dependencies": ["temp_provider"],

  "phm": { "period_ms": 1000, "allowed_missed_cycles": 3, "required_checkpoints": ["alive"] },

  "com": {
    "someip": {
      "service_id":  "0x5678",         // the service THIS app offers (Downstream)
      "instance_id": "0x0001",
      "event_group": "0x0001",
      "subscribe":   ["0x100"]         // Upstream event it listens to (from 0x1234/0x0001)
    }
  }
}
```
### 4. Update the `CMakeLists.txt` file. Template below.
```cmake
# --- temp_provider ---
add_executable(temp_provider apps/temp_provider.cpp)
target_include_directories(temp_provider PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/logging/include
  ${CMAKE_SOURCE_DIR}/persistency/include
  ${CMAKE_SOURCE_DIR}/phm/include
  ${CMAKE_SOURCE_DIR}/services
)
target_link_libraries(temp_provider PRIVATE
  logging
  persistency         # if you use storage
  ara_phm
  ara_com_adapter_someip
)

# --- temp_subscriber ---
add_executable(temp_subscriber apps/temp_subscriber.cpp)
target_include_directories(temp_subscriber PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/logging/include
  ${CMAKE_SOURCE_DIR}/persistency/include
  ${CMAKE_SOURCE_DIR}/phm/include
  ${CMAKE_SOURCE_DIR}/services
)
target_link_libraries(temp_subscriber PRIVATE
  logging
  persistency
  ara_phm
  ara_com_adapter_someip
)

# --- temp_bridge ---
add_executable(temp_bridge apps/temp_bridge.cpp)
target_include_directories(temp_bridge PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/logging/include
  ${CMAKE_SOURCE_DIR}/persistency/include
  ${CMAKE_SOURCE_DIR}/phm/include
  ${CMAKE_SOURCE_DIR}/services
)
target_link_libraries(temp_bridge PRIVATE
  logging
  persistency
  ara_phm
  ara_com_adapter_someip
)
```
### 5. Add your someip client ID
Update the local.json with your app.
```json
{
  "applications": [
    { "name": "execution_manager", "id": "0x0001" },
    { "name": "temp_provider",     "id": "0x1001" },
    { "name": "temp_subscriber",   "id": "0x1002" },
    { "name": "temp_bridge",       "id": "0x1003" }
  ],
  "...": "..."
}

```
Make sure all the apps you are running are there.

## Generic SocketCAN Gateway.
Include the following in your app.

```cpp
#include "can_gateway/include/function_bus_api.hpp"

// Keep this handle somewhere global/singleton-ish
static fn::GatewayHandle g_can;

void wire_can_gateway() {
  fn::BusToFunction b2f {
    /*setLineA*/ [](bool v){ function_setLineA(v); },  // your neutral setters
    /*setLineB*/ [](bool v){ function_setLineB(v); }
  };
  g_can = make_can_gateway(std::getenv("CAN_CONFIG_PATH"), b2f);
}

// Later, when your app wants to signal something to the bus. Examples:
void function_emitIndicator(fn::LightPattern p) { if (g_can.emitLight) g_can.emitLight(p); }
void function_emitActuator(fn::ActuatorCommand c)  { if (g_can.emitActuator)  g_can.emitActuator(c); }
```

### 1) Bring up a virtual CAN (Linux)
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0

### 2) Build & run
mkdir -p build && cd build
cmake .. && make -j
./can_gateway  # uses config/can-example.json by default

### 3) See TX frames
# In another terminal
candump vcan0

### 4) Inject RX frames (to simulate sensors -> app)
# Toggle LineA (bit 0) on CAN ID 0x300 (768)
cansend vcan0 300#01
cansend vcan0 300#00

## Useful stubs
Under `stubs/` you find some stubs that simulate different external sources such as user interface, simulating positioning input as if from GPS, and a cloud service example. These can be modified to implement real use cases.

Usage:
1) Build stubs:
```bash
cmake -S . -B build && cmake --build build -j
```
2) Run cloud receiver
```bash
./build/cloud_stub
```
3) Start position feed (optional overrides shown)
```bash
POS_LAT=59.33 POS_LON=18.07 POS_PERIOD_MS=1000 ./build/position_stub
```
4) Press the UI button (one-shot)
```bash
./build/ui_stub BUTTON
```
or interactive: 
```bash
./build/ui_stub   
```
(then type BUTTON / RESET)

5) Your app:
- listens to UI on UDP 127.0.0.1:15000 (env UI_PORT)
- listens to Position on UDP 127.0.0.1:17000 (env POS_RX_PORT)
- sends Cloud events to UDP 127.0.0.1:19000 (env CLOUD_HOST/CLOUD_PORT)
