#include <iostream>
#include <fstream>
#include <filesystem>
#include <map>
#include <vector>
#include <nlohmann/json.hpp>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <unordered_map>
#include <log.hpp>
#include <sinks_console.hpp>
#include <persistency/storage_registry.hpp>
#include "someip_binding.hpp"
#include <vsomeip/vsomeip.hpp>
#include <phm/phm_ids.hpp>
#include <phm/phm_supervisor.hpp>
#include <arpa/inet.h>
#include <cstring>
#include <csignal>
#include <chrono>
#include <thread>
#include <deque>
#include <sstream>
#include <cstdlib>
#include <unordered_set>


// #include "sinks_dlt.hpp"   // enable later when we can start dlt-daemon

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace ara::log;
using persistency::StorageRegistry;
using persistency::StorageType;

// App configuration structure
struct AppConfig {
    std::string app_id;
    std::string executable;
    bool start_on_boot;
    std::string restart_policy;
    std::string log_file;
    // Extending to handle manifests vs runtime usage gaps
    std::vector<std::string> dependencies;
    struct{
        int period_ms{1000};
        int allowed_missed_cycles{3};
        std::vector<std::uint32_t> required_checkpoints;
        bool require_alive{false};
    }phm{};
    struct{
        uint16_t service_id{0};
        uint16_t instance_id{0};
        uint16_t event_group{0x0001}; // default event group
        std::vector<uint16_t> subscribe_events;
    }com{};
};

// Build SOMEIP_REQUEST_EVENTS env var from manifest for this app.
// Format: "svc:inst:event[@group],svc:inst:event[@group],..."
static std::string build_someip_env(const AppConfig& a) {
    if (a.com.subscribe_events.empty() || a.com.service_id == 0 || a.com.instance_id == 0)
        return {};

    std::ostringstream oss;
    oss << std::showbase << std::hex;  // print numbers as 0x...
    bool first = true;
    for (auto ev : a.com.subscribe_events) {
        if (!first) oss << ",";
        first = false;
        oss << a.com.service_id << ":" << a.com.instance_id << ":" << ev
            << "@" << a.com.event_group;
    }
    return oss.str();
}

static std::unordered_map<uint16_t, std::string>
build_client_to_appid_map(const std::string& vsomeip_config_path,
                          const std::vector<AppConfig>& apps) {
    std::unordered_map<uint16_t, std::string> out;

    std::ifstream in(vsomeip_config_path);
    if (!in) {
        std::cerr << "[EM] Could not open VSOMEIP config: " << vsomeip_config_path << "\n";
        return out;
    }
    json j; in >> j;
    if (!j.contains("applications") || !j["applications"].is_array()) return out;

    std::unordered_set<std::string> manifest_ids;
    for (const auto& a : apps) if (!a.app_id.empty()) manifest_ids.insert(a.app_id);

    for (const auto& a : j["applications"]) {
        if (!a.is_object()) continue;
        const std::string name = a.value("name", "");
        const std::string id_s = a.value("id", "0");
        if (name.empty() || id_s.empty()) continue;

        // accepts "0x5555" or decimal
        uint16_t cid = static_cast<uint16_t>(std::stoul(id_s, nullptr, 0));
        if (manifest_ids.count(name)) out[cid] = name;
    }
    return out;
}


//Monitoring apps
struct AppMonitor {
    PhmSupervisor sup;
    // constructor that injects config
    explicit AppMonitor(const AppConfig& cfg) {
        PhmSupervisor::Config c;
        c.supervision_cycle_ms = cfg.phm.period_ms;
        c.allowed_missed_cycles = cfg.phm.allowed_missed_cycles;
        c.required_checkpoints = cfg.phm.required_checkpoints;
        sup = PhmSupervisor(c);
    }
    bool require_alive{false}; // separate flag from manifest
};

// Load manifest files
std::vector<AppConfig> load_manifests(const std::string& path) {
    std::vector<AppConfig> apps;

    for (const auto& entry : fs::directory_iterator(path)) {
        if (entry.path().extension() != ".json")
            continue;

        std::ifstream file(entry.path());
        if (!file) {
            std::cerr << "Error opening manifest: " << entry.path() << "\n";
            continue;
        }

        json j; file >> j;

        AppConfig app{};
        app.app_id        = j.value("app_id", "");
        app.executable    = j.value("executable", "");
        app.start_on_boot = j.value("start_on_boot", false);
        app.restart_policy= j.value("restart_policy", "never");
        app.log_file      = j.value("log_file", "");

        // dependencies
        if (j.contains("dependencies") && j["dependencies"].is_array()) {
            for (const auto& x : j["dependencies"])
                if (x.is_string()) app.dependencies.push_back(x.get<std::string>());
        }

        // phm
        if (j.contains("phm") && j["phm"].is_object()) {
            const auto& p = j["phm"];
            app.phm.period_ms             = p.value("period_ms", 1000);
            app.phm.allowed_missed_cycles = p.value("allowed_missed_cycles", 3);

            if (p.contains("required_checkpoints") && p["required_checkpoints"].is_array()) {
                for (const auto& it : p["required_checkpoints"]) {
                    if (it.is_string()) {
                        const auto s = it.get<std::string>();
                        if (s == "alive") app.phm.require_alive = true;
                        else app.phm.required_checkpoints.push_back(static_cast<uint32_t>(std::stoul(s, nullptr, 0)));
                    } else if (it.is_number_unsigned()) {
                        app.phm.required_checkpoints.push_back(it.get<uint32_t>());
                    }
                }
            }
        }

        // com.someip
        auto parse_u16 = [](const nlohmann::json& v, uint16_t def=0) -> uint16_t {
            if (v.is_number_unsigned()) return static_cast<uint16_t>(v.get<unsigned>());
            if (v.is_string())          return static_cast<uint16_t>(std::stoul(v.get<std::string>(), nullptr, 0));
            return def;
        };

        if (j.contains("com") && j["com"].is_object()) {
            const auto& c = j["com"];
            if (c.contains("someip") && c["someip"].is_object()) {
                const auto& s = c["someip"];
                if (s.contains("service_id"))  app.com.service_id  = parse_u16(s["service_id"]);
                if (s.contains("instance_id")) app.com.instance_id = parse_u16(s["instance_id"]);
                app.com.event_group = s.contains("event_group") ? parse_u16(s["event_group"], 0x0001) : 0x0001;

                if (s.contains("subscribe") && s["subscribe"].is_array()) {
                    for (const auto& e : s["subscribe"]) {
                        if (e.is_number_unsigned())
                            app.com.subscribe_events.push_back(static_cast<uint16_t>(e.get<unsigned>()));
                        else if (e.is_string())
                            app.com.subscribe_events.push_back(static_cast<uint16_t>(std::stoul(e.get<std::string>(), nullptr, 0)));
                    }
                }
            }
        }

        // Skip non-app JSONs (e.g., persistency.json)
        if (app.app_id.empty() || app.executable.empty())
            continue;

        apps.push_back(app);
    }

    return apps;
}


// Launch application and return PID
pid_t launch_app(const AppConfig& app, const std::string& extra_env = {}) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        if (!extra_env.empty()){
            ::setenv("SOMEIP_REQUEST_EVENTS", extra_env.c_str(), 1);
        }
        execl(app.executable.c_str(), app.executable.c_str(), nullptr);
        perror("execl failed");
        exit(1);
    } else if (pid > 0) {
        // Parent process
        std::cout << "[EM] Launched app: " << app.app_id << " (PID " << pid << ")" << std::endl;
        return pid;
    } else {
        perror("fork failed");
        return -1;
    }
}

static void register_phm_handlers(
    std::unordered_map<std::string, AppMonitor>& mon_by_app,
    const std::unordered_map<uint16_t, std::string>& app_by_client) {

    someip::register_rpc_handler(
        [&](uint16_t sid, uint16_t iid, uint16_t mid,
            const std::string& payload,
            std::shared_ptr<vsomeip::message> req) {

            if (sid != phm_ids::kService || iid != phm_ids::kInstance) return;
            const uint16_t client = req ? req->get_client() : 0u;

            auto it_map = app_by_client.find(client);
            if (it_map == app_by_client.end()) {
                std::cerr << "[PHM] Unknown client 0x" << std::hex << client
                          << std::dec << " — ignoring\n";
                someip::send_response(req);
                return;
            }
            const std::string& app_id = it_map->second;

            auto it_mon = mon_by_app.find(app_id);
            if (it_mon == mon_by_app.end()) {
                std::cerr << "[PHM] No supervisor for app_id=" << app_id << "\n";
                someip::send_response(req);
                return;
            }
            auto& sup = it_mon->second.sup;

            switch (mid) {
                case phm_ids::kAlive:
                    sup.on_alive();
                    someip::send_response(req);
                    break;

                case phm_ids::kCheckpoint:
                    if (payload.size() == 4) {
                        uint32_t cp;
                        std::memcpy(&cp, payload.data(), 4);
                        sup.on_checkpoint(ntohl(cp));
                    }
                    someip::send_response(req);
                    break;

                default:
                    someip::send_response(req);
                    break;
            }
        }
    );
}


//Used for better shutdown behavior
static std::atomic_bool running{true};
static void on_sig(int){ running=false; }

int main() {

    std::string manifest_dir = "../manifests";
    std::string config_path  = manifest_dir + "/persistency.json";

    //For better shutdown
    std::signal(SIGINT, on_sig);
    std::signal(SIGTERM, on_sig);

    auto r = persistency::StorageRegistry::Instance().InitFromFile(config_path);
    if (!r.HasValue()) {
        std::cerr << "[EM] Failed to load persistency registry from " << config_path << "\n";
        return 1;
    }
    
    LogManager::Instance().SetGlobalIds("ECU1", "EMGR");
    LogManager::Instance().SetDefaultLevel(LogLevel::kInfo);
    LogManager::Instance().AddSink(std::make_shared<ConsoleSink>());
    // LogManager::Instance().AddSink(std::make_shared<DltSink>("Execution Manager")); // when ready

    auto log = Logger::CreateLogger("EM", "Execution Manager");
    ARA_LOGINFO(log, "Execution Manager starting…");

    //Health management phm stuff
    someip::init("phm_supervisor"); // start vsomeip
    someip::offer_service(phm_ids::kService, phm_ids::kInstance, /*event_id*/ 0x0100, /*event_group_id*/ 0x0001);
    
    //Removed to enable multiple handlers
    //PhmSupervisor phm;
    //register_phm_handlers(phm);
    //phm.maintenance_tick(); //Moved to loop below
    // Periodic PHM maintenance (100 ms cadence; adjust according to system requirements)
    using clock = std::chrono::steady_clock;
    const auto tick_period = std::chrono::milliseconds(100);
    auto next_tick = clock::now() + tick_period;

    // Maps
    std::unordered_map<std::string, AppConfig>  app_by_id;    // app_id -> config
    std::unordered_map<std::string, AppMonitor> mon_by_app;   // app_id -> supervisor wrapper
    std::unordered_map<uint16_t, std::string>   app_by_client; // vsomeip client id -> app_id

    auto apps = load_manifests(manifest_dir);

    for (const auto& a : apps) {
        if (a.app_id.empty()) continue;

        // Keep the raw config
        app_by_id.emplace(a.app_id, a);

        // Build a monitor only if PHM is requested (or build for all apps if you prefer)
        const bool has_phm =
            (a.phm.period_ms > 0) ||
            a.phm.require_alive ||
            !a.phm.required_checkpoints.empty(); //TODO: required checkpoints seem redundant? Remove later

        if (has_phm) {
            AppMonitor m(a);                         // uses AppConfig-based ctor
            m.require_alive = a.phm.require_alive;   // carry the flag if you use it separately
            m.sup.set_violation_callback([aid = a.app_id](const char* reason){
                std::cerr << "[PHM] Violation in " << aid << ": "
                        << (reason ? reason : "") << "\n";
            });
            mon_by_app.emplace(a.app_id, std::move(m)); // insert once; no operator[] + emplace combo
        }
    }

    //Build maps
    const char* cfg_env = std::getenv("VSOMEIP_CONFIGURATION");
    std::string vs_cfg = cfg_env ? cfg_env : "vsomeip/local.json";
    app_by_client = build_client_to_appid_map(vs_cfg, apps);

    // Call to regiester handlers
    register_phm_handlers(mon_by_app, app_by_client);

    std::map<pid_t, AppConfig> running_apps;
    std::map<std::string, int> restart_count;
    const int max_restarts = 3;

    // Start apps marked with start_on_boot
    // Removing (but keep for reference if it doesn't work) to handle multiple handlers
    //for (const auto& app : apps) {
    //    if (app.start_on_boot) {
    //        pid_t pid = launch_app(app);
    //        if (pid > 0) {
    //            running_apps[pid] = app;
    //            restart_count[app.app_id] = 0;
    //        }
    //    }
    //}

    // simple Kahn topological sort over start_on_boot apps
    std::unordered_map<std::string,int> indeg;
    std::unordered_map<std::string,std::vector<std::string>> g;

    for (const auto& a : apps) if (a.start_on_boot) indeg[a.app_id] = 0;

    // Add edges for dependencies (only among start_on_boot apps)
    for (const auto& a : apps) if (a.start_on_boot) {
        for (const auto& d : a.dependencies) {
            if (d == a.app_id) {
                std::cerr << "[EM] Self-dependency ignored: " << a.app_id << "\n";
                continue;
            }
            auto dep_it = app_by_id.find(d);
            if (dep_it == app_by_id.end()) {
                std::cerr << "[EM] Unknown dependency '" << d
                        << "' referenced by " << a.app_id << " (ignored)\n";
                continue;
            }
            if (!dep_it->second.start_on_boot) {
                std::cerr << "[EM] " << a.app_id << " depends on '" << d
                        << "' which is not start_on_boot (constraint ignored)\n";
                continue;
            }
            g[d].push_back(a.app_id);
            indeg[a.app_id]++;              // <- your original line
        }
    }

    std::deque<std::string> q;
    for (auto& [n,deg] : indeg) if (deg == 0) q.push_back(n);

    std::vector<std::string> topo;
    while (!q.empty()) {
        auto u = q.front(); q.pop_front();
        topo.push_back(u);
        for (auto& v : g[u]) if (--indeg[v] == 0) q.push_back(v);
    }

    if (topo.size() != indeg.size()) {
        std::cerr << "[EM] Dependency cycle detected; starting in manifest order.\n";
        topo.clear();
        for (const auto& a : apps) if (a.start_on_boot) topo.push_back(a.app_id);
    }

    // Launch respecting order

    for (const auto& id : topo) {
        const auto& app = app_by_id[id];
        const auto env = build_someip_env(app);
        pid_t pid = launch_app(app, env);
        if (pid > 0) {
            running_apps[pid] = app;
            restart_count[app.app_id] = 0;
        }
    }


    // Monitor running apps (signal-aware, non-blocking)
    using namespace std::chrono_literals;

    while (running) {
        // PHM periodic tick
        //Run at ~100 ms regardless of child process activity
        auto now = clock::now();
        if (now >= next_tick) {
            // If we were busy (e.g., reaping), catch up without drifting.
            do {
                for (auto& [aid, mon] : mon_by_app) {
                    mon.sup.maintenance_tick();
                }
                next_tick += tick_period;
                now = clock::now();
            } while (running && now >= next_tick);
        }
        int status = 0;
        pid_t pid = waitpid(-1, &status, WNOHANG);

        if (pid > 0) {
            auto it = running_apps.find(pid);
            if (it != running_apps.end()) {
                const auto app = it->second;
                running_apps.erase(it);

                std::cout << "[EM] App with PID " << pid << " exited with status " << status << std::endl;

                bool failed =
                (WIFEXITED(status) && WEXITSTATUS(status) != 0) ||
                (WIFSIGNALED(status));

                if (app.restart_policy == "on-failure" && failed) {
                    int& cnt = restart_count[app.app_id];
                    ++cnt;
                    if (cnt <= max_restarts) {
                        std::cout << "[EM] Restarting app: " << app.app_id
                                << " (Attempt " << cnt << ")" << std::endl;
                        const auto env = build_someip_env(app);
                        pid_t new_pid = launch_app(app, env);
                        if (new_pid > 0) {
                            running_apps[new_pid] = app;
                        }
                    } else {
                        std::cout << "[EM] Max restart attempts reached for app: " << app.app_id << std::endl;
                    }
                }
            }
            // keep looping; there might be more to reap
            continue;
        }

        if (pid == 0) {
            // no child changed -> short sleep to avoid busy spin
            std::this_thread::sleep_for(100ms);
            continue;
        }

        // pid < 0
        if (errno == ECHILD) {
            // no children left
            break;
        }
        // transient error: sleep a bit and retry
        std::this_thread::sleep_for(100ms);
    }

    // If we got here because of Ctrl-C, terminate remaining children gracefully
    if (!running && !running_apps.empty()) {
        std::cout << "[EM] Caught signal: shutting down children…" << std::endl;

        // 1) Ask nicely
        for (const auto& [pid, _] : running_apps)
            kill(pid, SIGTERM);

        // 2) Wait up to ~2s for them to exit
        for (int i = 0; i < 20; ++i) {
            int status = 0;
            pid_t r = waitpid(-1, &status, WNOHANG);
            if (r > 0) {
                running_apps.erase(r);
                if (running_apps.empty()) break;
            } else if (r == 0) {
                std::this_thread::sleep_for(100ms);
            } else if (errno == ECHILD) {
                running_apps.clear();
                break;
            }
        }

        // 3) Nuke any holdouts
        for (const auto& [pid, _] : running_apps)
            kill(pid, SIGKILL);

        // Reap anything that just died
        while (waitpid(-1, nullptr, WNOHANG) > 0) {}
    }

    // Stop vsomeip cleanly (requires the shutdown() you added in the binding)
    someip::shutdown();

    std::cout << "[EM] All apps have exited. Shutting down Execution Manager." << std::endl;
    return 0;
}
