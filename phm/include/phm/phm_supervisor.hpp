#pragma once
#include <chrono>
#include <cstdint>
#include <vector>

class PhmSupervisor {
public:
    struct Config {
        int supervision_cycle_ms{1000};
        int allowed_missed_cycles{3};
        int max_retries{5};
        int backoff_initial_ms{500};
        double backoff_factor{2.0};
        int backoff_max_ms{15000};
        int retry_reset_window_ms{60000};
        std::vector<std::uint32_t> required_checkpoints{};
    };

    PhmSupervisor() = default;                               // default ctor
    explicit PhmSupervisor(const Config& cfg) : cfg_(cfg) {} // NO default arg here

    void on_alive();
    void on_checkpoint(std::uint32_t id);
    void maintenance_tick();
    void set_violation_callback(std::function<void(const char* reason)> cb) { on_violation_ = std::move(cb); }
    
private:
    static bool contains_all(const std::vector<std::uint32_t>& have,
                             const std::vector<std::uint32_t>& need);

    Config cfg_{};  // keep a default-initialized copy here
    std::chrono::steady_clock::time_point cycle_start_{};
    bool got_alive_{false};
    std::vector<std::uint32_t> seen_cps_{};
    int missed_cycles_{0};
    int retries_{0};
    std::chrono::steady_clock::time_point last_healthy_{};
    std::function<void(const char* reason)> on_violation_{};
};
