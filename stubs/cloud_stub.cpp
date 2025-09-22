// stubs/ui_stub.cpp
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

static uint16_t get_port() {
  const char* p = std::getenv("UI_PORT");
  return p ? static_cast<uint16_t>(std::stoi(p)) : 15000;
}

static bool send_msg(const std::string& msg, uint16_t port) {
  int s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s < 0) { perror("socket"); return false; }
  sockaddr_in dst{}; dst.sin_family = AF_INET;
  dst.sin_port = htons(port);
  dst.sin_addr.s_addr = inet_addr("127.0.0.1");
  ssize_t n = sendto(s, msg.data(), (int)msg.size(), 0, (sockaddr*)&dst, sizeof(dst));
  close(s);
  return n >= 0;
}

int main(int argc, char** argv) {
  uint16_t port = get_port();

  if (argc > 1) {
    std::string msg = argv[1]; // "BUTTON" or "RESET"
    if (!send_msg(msg, port)) { std::cerr << "send failed\n"; return 1; }
    std::cout << "sent \"" << msg << "\" to 127.0.0.1:" << port << "\n";
    return 0;
  }

  // Interactive mode
  std::cout << "UI stub -> 127.0.0.1:" << port << "  (type BUTTON / RESET / quit)\n";
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line == "quit" || line == "exit") break;
    if (line != "BUTTON" && line != "RESET") {
      std::cout << "Type BUTTON, RESET, or quit\n";
      continue;
    }
    if (!send_msg(line, port)) std::cerr << "send failed\n";
  }
  return 0;
}
