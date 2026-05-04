#include <sys/socket.h>
#include <linux/un.h>
#include <netinet/in.h>
#include <unistd.h>
#include <iostream>
#include <string>
#include <string.h>
#include <unordered_map>

#include "ring_buffer.hpp"

static std::unordered_map<std::string, RingBuffer<std::string>> chat_messages;

std::string get_chat_name(int client_fd) {
  char buffer[1024];
  ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
  if (bytes_read < 0) {
    std::cout << "Failed to read from client" << std::endl;
    return "";
  } else {
    buffer[bytes_read] = '\0';
    std::cout << "Received message: " << buffer << std::endl;
    return std::string(buffer);
  }
}

int write_handler(int client_fd) {
  std::string chat_name = get_chat_name(client_fd);
  if (chat_name.empty()) {
    return -1;
  }

  char buffer[1024];
  ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
  if (bytes_read < 0) {
    std::cout << "Failed to read from client" << std::endl;
    return -1;
  } else {
    buffer[bytes_read] = '\0';
    std::cout << "Received message: " << buffer << std::endl;
    if (chat_messages.contains(chat_name)) {
      chat_messages[chat_name].Push(buffer);
    } else {
      chat_messages.try_emplace(chat_name, 10);
      chat_messages[chat_name].Push(buffer);
    }
  }

  return 0;
}

int read_handler(int client_fd) {
  std::string chat_name = get_chat_name(client_fd);
  if (chat_name.empty()) {
    return -1;
  }

  std::string message;
  if (chat_messages.contains(chat_name)) {
    for (size_t i = 0; i < chat_messages[chat_name].Size(); i++) {
      message += chat_messages[chat_name][i];
      message += "\n";
    }

    write(client_fd, message.c_str(), message.size());
  } else {
    std::string error_msg = "Chat not found";
    write(client_fd, error_msg.c_str(), error_msg.size());
  }

  return 0;
}

int main() {
  int server_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);

  struct sockaddr_un addr = {};
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, "/telegram.sock", sizeof(addr.sun_path) - 1);

  unlink("/telegram.sock");
  bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
  listen(server_fd, 5);

  while (true) {
    int client_fd = accept(server_fd, nullptr, nullptr);
    if (client_fd < 0) {
      std::cout << "Failed to accept connection" << std::endl;
      continue;
    }
    std::cout << "Client connected" << std::endl;

    char buffer[1024];
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read < 0) {
      std::cout << "Failed to read from client" << std::endl;
    } else {
      buffer[bytes_read] = '\0';
      if (strcmp(buffer, "write") == 0) {
        write_handler(client_fd);
      } else {
        read_handler(client_fd);
      }
    }

    close(client_fd);
  }
}
