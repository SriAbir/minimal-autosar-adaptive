#include <iostream>
#include <fstream>
#include <filesystem>
#include <map>
#include <vector>
#include <nlohmann/json.hpp>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

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
};

// Load manifest files
std::vector<AppConfig> load_manifests(const std::string& path) {
    std::vector<AppConfig> apps;
    for (const auto& entry : fs::directory_iterator(path)) {
        if (entry.path().extension() == ".json") {
            std::ifstream file(entry.path());
            if (!file) {
                std::cerr << "Error opening manifest: " << entry.path() << std::endl;
                continue;
            }

            json j;
            file >> j;

            AppConfig app {
                j.value("app_id", ""),
                j.value("executable", ""),
                j.value("start_on_boot", false),
                j.value("restart_policy", "never"),
                j.value("log_file", "")
            };

            apps.push_back(app);
        }
    }
    return apps;
}

// Launch application and return PID
pid_t launch_app(const AppConfig& app) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
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

static void register_phm_handlers(PhmSupervisor& phm) {
    someip::register_rpc_handler(
        [&](uint16_t sid, uint16_t iid, uint16_t mid,
            const std::string& payload,
            std::shared_ptr<vsomeip::message> req) {

            if (sid != phm_ids::kService || iid != phm_ids::kInstance) return;

            switch (mid) {
                case phm_ids::kAlive:
                    phm.on_alive();
                    someip::send_response(req);
                    break;

                case phm_ids::kCheckpoint:
                    if (payload.size() == 4) {
                        uint32_t cp;
                        std::memcpy(&cp, payload.data(), 4);
                        cp = ntohl(cp);
                        phm.on_checkpoint(cp);
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
    std::vector<pid_t> children;

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
    
    PhmSupervisor phm;
    register_phm_handlers(phm);
    phm.maintenance_tick(); //TODO: Only once, add loop

    auto apps = load_manifests(manifest_dir);

    std::map<pid_t, AppConfig> running_apps;
    std::map<std::string, int> restart_count;
    const int max_restarts = 3;

    // Start apps marked with start_on_boot
    for (const auto& app : apps) {
        if (app.start_on_boot) {
            pid_t pid = launch_app(app);
            if (pid > 0) {
                running_apps[pid] = app;
                restart_count[app.app_id] = 0;
            }
        }
    }

    // Monitor running apps (signal-aware, non-blocking)
    using namespace std::chrono_literals;

    while (running) {
        int status = 0;
        pid_t pid = waitpid(-1, &status, WNOHANG);

        if (pid > 0) {
            auto it = running_apps.find(pid);
            if (it != running_apps.end()) {
                const auto app = it->second;
                running_apps.erase(it);

                std::cout << "[EM] App with PID " << pid << " exited with status " << status << std::endl;

                if (app.restart_policy == "on-failure" && WIFEXITED(status) && WEXITSTATUS(status) != 0) {
                    int& cnt = restart_count[app.app_id];
                    ++cnt;
                    if (cnt <= max_restarts) {
                        std::cout << "[EM] Restarting app: " << app.app_id
                                << " (Attempt " << cnt << ")" << std::endl;
                        pid_t new_pid = launch_app(app);
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
