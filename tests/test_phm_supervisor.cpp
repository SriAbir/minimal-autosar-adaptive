#include <gtest/gtest.h>
#include <phm/phm_supervisor.hpp>
#include <thread>
#include <chrono>

using namespace std::chrono_literals;

static void tick_for(PhmSupervisor& phm, int ms, int step_ms = 2) {
    // Call maintenance_tick periodically for ~ms total
    const int steps = ms / step_ms + 1;
    for (int i = 0; i < steps; ++i) {
        phm.maintenance_tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(step_ms));
    }
}

TEST(PHM, HealthyWithAlive_NoViolation) {
    PhmSupervisor::Config cfg;
    cfg.supervision_cycle_ms = 10;   // small & fast cycles
    cfg.allowed_missed_cycles = 1;

    PhmSupervisor phm(cfg);

    bool violated = false;
    phm.set_violation_callback([&](const char*) { violated = true; });

    // Run for ~5 cycles; send alive each cycle
    for (int i = 0; i < 5; ++i) {
        phm.on_alive();
        tick_for(phm, 12); // > cycle length to roll the cycle
    }
    EXPECT_FALSE(violated);
}

TEST(PHM, MissedCycles_TriggersViolation) {
    PhmSupervisor::Config cfg;
    cfg.supervision_cycle_ms = 10;   // fast cycle
    cfg.allowed_missed_cycles = 1;   // tolerate 1 miss, violate on 2nd

    PhmSupervisor phm(cfg);

    int violation_count = 0;
    phm.set_violation_callback([&](const char*) { violation_count++; });

    // Don’t send alive => after >2 cycles, should trigger
    tick_for(phm, 30);  // ~3 cycles with step ~2ms inside helper

    EXPECT_GE(violation_count, 1);
}

TEST(PHM, RequiredCheckpoints_MissingThenPresent) {
    PhmSupervisor::Config cfg;
    cfg.supervision_cycle_ms = 10;
    cfg.allowed_missed_cycles = 0;     // violate on first bad cycle
    cfg.required_checkpoints = {1001, 1002};

    PhmSupervisor phm(cfg);

    int violation_count = 0;
    phm.set_violation_callback([&](const char*) { violation_count++; });

    // 1) Missing checkpoints (alive only) -> violation
    phm.on_alive();
    tick_for(phm, 12); // one full cycle
    EXPECT_EQ(violation_count, 1);

    // 2) Next cycle: alive + all required checkpoints -> healthy
    phm.on_alive();
    phm.on_checkpoint(1001);
    phm.on_checkpoint(1002);
    tick_for(phm, 12);
    EXPECT_EQ(violation_count, 1); // still 1 => no new violation
}
