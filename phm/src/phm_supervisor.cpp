#include <algorithm>
#include <cmath>
#include <iostream>
#include <phm/phm_supervisor.hpp>

void PhmSupervisor::on_alive() {
    got_alive_ = true;
}

void PhmSupervisor::on_checkpoint(std::uint32_t id) {
    seen_cps_.push_back(id);
}

bool PhmSupervisor::contains_all(const std::vector<std::uint32_t>& have,
                                 const std::vector<std::uint32_t>& need) {
    if (need.empty()) return true;
    for (auto n : need) {
        if (std::find(have.begin(), have.end(), n) == have.end())
            return false;
    }
    return true;
}

void PhmSupervisor::maintenance_tick() {
    const auto now = std::chrono::steady_clock::now();

    if (cycle_start_.time_since_epoch().count() == 0) {
        cycle_start_ = now;
        last_healthy_ = now;
        return;
    }

    const auto cycle_len = std::chrono::milliseconds(cfg_.supervision_cycle_ms);
    if ((now - cycle_start_) >= cycle_len) {
        const bool cps_ok = contains_all(seen_cps_, cfg_.required_checkpoints);
        const bool alive_ok = cfg_.required_checkpoints.empty() ? got_alive_ : (got_alive_ && cps_ok);

        if (alive_ok) {
            missed_cycles_ = 0;
            last_healthy_ = now;
        } else {
            missed_cycles_++;
            std::cerr << "[PHM] Missed supervision cycle " << missed_cycles_ << "\n";
            if (missed_cycles_ > cfg_.allowed_missed_cycles) {
                // Here we could trigger a controlled restart + backoff.
                // For now, just set violation:
                if (on_violation_) on_violation_("supervision violation");
                missed_cycles_ = 0;
            }
        }

        // start next cycle
        cycle_start_ = now;
        got_alive_ = false;
        seen_cps_.clear();
    }
}