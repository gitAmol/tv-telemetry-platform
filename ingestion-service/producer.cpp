#include <iostream>
#include <string>

int main() {
    std::cout << "Starting Telemetry Producer..." << std::endl;

    std::string event = R"({
        "device_id": "TV123",
        "event_type": "bluetooth",
        "event_action": "connect"
    })";

    std::cout << "Sending event: " << event << std::endl;

    return 0;
}