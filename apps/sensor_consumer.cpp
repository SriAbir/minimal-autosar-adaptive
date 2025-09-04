#include "someip_binding.hpp"
#include <iostream>
#include <thread>

int main() {
    std::cout << "[sensor_consumer] Starting..." << std::endl;

    // Initialize the vsomeip app via shim
    someip::init("sensor_consumer");

    // Request the service
    someip::request_service(0x1234, 0x5678);

    // Subscribe to the correct event group after discovery
    someip::subscribe_to_event(0x1234, 0x5678, 0x01, 0x1000);
 // must match what's in vsomeip.json

    // Register message handler
    someip::register_handler([](const std::string& msg) {
        std::cout << "[sensor_consumer] Received: " << msg << std::endl;
    });

    // Keep app alive
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}
