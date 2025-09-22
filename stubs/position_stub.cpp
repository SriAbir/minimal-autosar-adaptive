// stubs/position_stub.cpp
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

static uint16_t tx_port() {
  const char* p = std::getenv("POS_RX_PORT"); // app listens here
  return p ? static_cast<uint16_t>(std::stoi(p)) : 17000;
}
static double env_double(const char* name, double defv) {
  if (const char* v = std::getenv(name)) try { return std::stod(v); } catch (...) {}
  return defv;
}
static int env_int(const char* name, int defv) {
  if (const char* v = std::getenv(name)) try { return std::stoi(v); } catch (...) {}
  return defv;
}

int main() {
  const double lat = env_double("POS_LAT", 59.3293);   // Stockholm default
  const double lon = env_double("POS_LON", 18.0686);
  const int period_ms = env_int("POS_PERIOD_MS", 2000);
  const uint16_t port = tx_port();

  int s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s < 0) { perror("socket"); return 1; }
  sockaddr_in dst{}; dst.sin_family = AF_INET;
  dst.sin_port = htons(port);
  dst.sin_addr.s_addr = inet_addr("127.0.0.1");

  std::cout << "position_stub -> 127.0.0.1:" << port
            << " lat=" << lat << " lon=" << lon
            << " period_ms=" << period_ms << "\n";

  while (true) {
    std::time_t ts = std::time(nullptr);
    std::ostringstream oss;
    oss << "{\"lat\":" << lat << ",\"lon\":" << lon << ",\"ts\":" << ts << "}";
    std::string msg = oss.str();
    if (sendto(s, msg.data(), (int)msg.size(), 0, (sockaddr*)&dst, sizeof(dst)) < 0)
      perror("sendto");
    std::this_thread::sleep_for(std::chrono::milliseconds(period_ms));
  }
}
