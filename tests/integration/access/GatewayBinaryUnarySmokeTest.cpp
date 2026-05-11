#include <cerrno>
#include <csignal>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "httplib.h"

#include "support/TestAssertions.h"

#ifndef DASALL_GATEWAY_BINARY_PATH
#error "DASALL_GATEWAY_BINARY_PATH must be defined"
#endif

#ifndef DASALL_REPOSITORY_ROOT
#error "DASALL_REPOSITORY_ROOT must be defined"
#endif

namespace {

namespace fs = std::filesystem;

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

class ScopedTempDirectory {
 public:
  explicit ScopedTempDirectory(std::string stem)
      : path_(fs::temp_directory_path() /
              (std::move(stem) + "-" + std::to_string(::getpid()) + "-" +
               std::to_string(std::chrono::steady_clock::now()
                                  .time_since_epoch()
                                  .count()))) {
    fs::create_directories(path_);
    fs::permissions(path_, static_cast<fs::perms>(0700), fs::perm_options::replace);
  }

  ~ScopedTempDirectory() {
    std::error_code error;
    fs::remove_all(path_, error);
  }

  [[nodiscard]] const fs::path& path() const {
    return path_;
  }

 private:
  fs::path path_;
};

[[nodiscard]] int reserve_loopback_port() {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    throw std::runtime_error("failed to create port reservation socket");
  }

  sockaddr_in address {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  if (::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
    (void)::close(fd);
    throw std::runtime_error("failed to bind port reservation socket");
  }

  socklen_t length = sizeof(address);
  if (::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
    (void)::close(fd);
    throw std::runtime_error("failed to read reserved port");
  }

  const int port = static_cast<int>(ntohs(address.sin_port));
  (void)::close(fd);
  return port;
}

class ScopedGatewayProcess {
 public:
  ScopedGatewayProcess(std::string binary_path,
                       fs::path working_directory,
                       int port,
                       fs::path log_path)
      : log_path_(std::move(log_path)) {
    pid_ = start(std::move(binary_path),
                 std::move(working_directory),
                 port,
                 log_path_);
  }

  ScopedGatewayProcess(const ScopedGatewayProcess&) = delete;
  ScopedGatewayProcess& operator=(const ScopedGatewayProcess&) = delete;

  ~ScopedGatewayProcess() {
    if (pid_ > 0) {
      (void)stop();
    }
  }

  [[nodiscard]] int stop() {
    if (pid_ <= 0) {
      return exit_code_;
    }

    if (::kill(pid_, SIGTERM) != 0 && errno != ESRCH) {
      throw std::runtime_error("failed to terminate gateway process");
    }

    int status = 0;
    if (::waitpid(pid_, &status, 0) < 0) {
      throw std::runtime_error("failed to wait for gateway process");
    }

    pid_ = -1;
    if (WIFEXITED(status)) {
      exit_code_ = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
      exit_code_ = 128 + WTERMSIG(status);
    }
    return exit_code_;
  }

  [[nodiscard]] std::string read_log() const {
    std::ifstream stream(log_path_);
    return std::string(std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>());
  }

 private:
  [[nodiscard]] static pid_t start(std::string binary_path,
                                   fs::path working_directory,
                                   int port,
                                   const fs::path& log_path) {
    const pid_t pid = ::fork();
    if (pid < 0) {
      throw std::runtime_error("failed to fork gateway process");
    }

    if (pid == 0) {
      const int log_fd = ::open(log_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
      if (log_fd < 0) {
        std::perror("open");
        std::_Exit(127);
      }

      ::dup2(log_fd, STDOUT_FILENO);
      ::dup2(log_fd, STDERR_FILENO);
      ::close(log_fd);

      if (::chdir(working_directory.c_str()) != 0) {
        std::perror("chdir");
        std::_Exit(127);
      }

      const std::string port_arg = std::to_string(port);
      std::vector<std::string> args = {
          std::move(binary_path),
          "--port",
          port_arg,
          "--profile-id",
          "desktop_full",
      };
      std::vector<char*> argv;
      argv.reserve(args.size() + 1);
      for (auto& arg : args) {
        argv.push_back(arg.data());
      }
      argv.push_back(nullptr);

      ::execv(argv.front(), argv.data());
      std::perror("execv");
      std::_Exit(127);
    }

    return pid;
  }

  pid_t pid_ = -1;
  int exit_code_ = -1;
  fs::path log_path_;
};

[[nodiscard]] bool wait_for_ready(const int port) {
  httplib::Client client("127.0.0.1", port);
  client.set_connection_timeout(1, 0);
  client.set_read_timeout(1, 0);

  for (int attempt = 0; attempt < 100; ++attempt) {
    if (auto response = client.Get("/health/ready")) {
      if (response->status == 200 && response->body.starts_with("READY")) {
        return true;
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  return false;
}

void gateway_binary_unary_smoke_completes_with_real_main_init() {
  ScopedTempDirectory temp_root("dasall-gateway-binary-unary");
  const int port = reserve_loopback_port();
  const auto log_path = temp_root.path() / "gateway.log";

  ScopedGatewayProcess gateway(
      DASALL_GATEWAY_BINARY_PATH,
      temp_root.path(),
      port,
      log_path);

  if (!wait_for_ready(port)) {
    const auto gateway_exit_code = gateway.stop();
    assert_true(false,
                "gateway binary smoke should observe /health/ready before submit; port=" +
                    std::to_string(port) +
                    " artifact_path=" + log_path.string() +
                    " gateway_exit_code=" + std::to_string(gateway_exit_code) +
                    " gateway_log=" + gateway.read_log());
  }

  {
    httplib::Client readiness_client("127.0.0.1", port);
    readiness_client.set_connection_timeout(1, 0);
    readiness_client.set_read_timeout(1, 0);
    const auto readiness = readiness_client.Get("/health/ready");
    assert_true(static_cast<bool>(readiness),
                "gateway binary smoke should receive a readiness response; gateway_log=" +
                    gateway.read_log() + " artifact_path=" + log_path.string());
    assert_equal(200,
                 readiness->status,
                 "gateway binary smoke should keep degraded runtime path health-ready but explicitly labeled; body=" +
                     readiness->body + " gateway_log=" + gateway.read_log() +
                     " artifact_path=" + log_path.string());
    assert_true(readiness->body.find("runtime_readiness=degraded-ready") != std::string::npos,
                "gateway readiness body should expose degraded runtime readiness; body=" +
                    readiness->body + " gateway_log=" + gateway.read_log() +
                    " artifact_path=" + log_path.string());
    assert_true(readiness->body.find("stub-ready") == std::string::npos,
                "gateway readiness body should not expose stub-ready on app-binary path; body=" +
                    readiness->body + " gateway_log=" + gateway.read_log() +
                    " artifact_path=" + log_path.string());
  }

  httplib::Client client("127.0.0.1", port);
  client.set_connection_timeout(1, 0);
  client.set_read_timeout(1, 0);

  const auto response = client.Post(
      "/v1/submit",
      R"({"packet_id":"gateway-binary-smoke","entry_type":"gateway","peer_ref":"jwt:user://tenant-a/alice","payload":"binary smoke","trace_id":"gateway-binary-smoke-trace","session_hint":"gateway-binary-smoke-session"})",
      "application/json");

  assert_true(static_cast<bool>(response),
              "gateway binary smoke should receive an HTTP response; gateway_log=" +
                  gateway.read_log() + " artifact_path=" + log_path.string());
  assert_equal(200,
               response->status,
               "gateway binary smoke should complete POST /v1/submit through the built gateway main path; body=" +
                   response->body + " gateway_log=" + gateway.read_log() +
                   " artifact_path=" + log_path.string());
  assert_true(response->body.find("\"result_id\":\"") != std::string::npos,
              "gateway binary smoke should surface a result_id from runtime backend handoff; body=" +
                  response->body + " gateway_log=" + gateway.read_log() +
                  " artifact_path=" + log_path.string());
  assert_true(response->body.find("\"status\":\"200\"") != std::string::npos,
              "gateway binary smoke should preserve protocol status hint 200 in the HTTP envelope; body=" +
                  response->body + " gateway_log=" + gateway.read_log() +
                  " artifact_path=" + log_path.string());
  assert_true(response->body.find("\"payload\":\"") != std::string::npos,
              "gateway binary smoke should surface non-empty runtime payload in the HTTP envelope; body=" +
                  response->body + " gateway_log=" + gateway.read_log() +
                  " artifact_path=" + log_path.string());

  const auto gateway_exit_code = gateway.stop();
  const auto gateway_log = gateway.read_log();
  assert_true(gateway_log.find("[dasall_gateway] runtime readiness=") != std::string::npos,
              "gateway binary smoke should pass through the real gateway main init path; gateway_exit_code=" +
                  std::to_string(gateway_exit_code) + " artifact_path=" + log_path.string() +
                  " gateway_log=" + gateway_log);
  assert_true(gateway_log.find("[dasall_gateway] runtime readiness=degraded-ready") !=
                  std::string::npos,
              "gateway binary smoke should log degraded-ready instead of accepted-only readiness; gateway_exit_code=" +
                  std::to_string(gateway_exit_code) + " artifact_path=" + log_path.string() +
                  " gateway_log=" + gateway_log);
  assert_true(gateway_log.find("runtime readiness=stub-ready") == std::string::npos,
              "gateway binary smoke should fail if gateway main falls back to stub-ready; gateway_exit_code=" +
                  std::to_string(gateway_exit_code) + " artifact_path=" + log_path.string() +
                  " gateway_log=" + gateway_log);
}

}  // namespace

int main() {
  try {
    gateway_binary_unary_smoke_completes_with_real_main_init();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}