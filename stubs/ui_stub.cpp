// stubs/cloud_stub.cpp
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <iostream>

static uint16_t rx_port() {
  const char* p = std::getenv("CLOUD_PORT");
  return p ? static_cast<uint16_t>(std::stoi(p)) : 19000;
}
static const char* rx_host() {
  // For UDP bind, we always use 127.0.0.1 as local endpoint
  return "127.0.0.1";
}

int main() {
  uint16_t port = rx_port();

  int s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s < 0) { perror("socket"); return 1; }

  sockaddr_in addr{}; addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = inet_addr(rx_host());
  if (bind(s, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); close(s); return 1; }

  std::cout << "cloud_stub listening on " << rx_host() << ":" << port << "\n";

  char buf[8192];
  while (true) {
    ssize_t n = recv(s, buf, sizeof(buf)-1, 0);
    if (n <= 0) break;
    buf[n] = 0;
    std::cout << "EVENT: " << buf << std::endl;
  }
  close(s);
  return 0;
}
