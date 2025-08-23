#include <iostream>
#include <thread>
#include <chrono>

int main() {
    std::cout << "[sensor_provider] Running..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(10));
    std::cout << "[sensor_provider] Exiting." << std::endl;
    return 0;
}
