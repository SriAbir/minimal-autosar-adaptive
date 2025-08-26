#include "someip_binding.hpp"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    std::cout << "[someip_provider] Starting..." << std::endl;

    // ✅ Initialize vsomeip application using the shim
    someip::init("someip_provider");

    // ✅ Offer the service, event, and group through the shim
    someip::offer_service(0x1234, 0x5678, 0x1000, 0x01);  // service_id, instance_id, event_id, event_group_id

    // ✅ Loop to send notifications
    int counter = 0;
    while (true) {
        std::string msg = "Sensor value: " + std::to_string(100 + counter++);
        someip::send_notification(0x1234, 0x5678, 0x1000, msg);
        std::cout << "[someip_provider] Sent: " << msg << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    return 0;
}
