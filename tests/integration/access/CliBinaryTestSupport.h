#pragma once

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

namespace dasall::tests::integration::access_support {

struct ProcessResult {
  int exit_code = -1;
  std::string stdout_text;
  std::string stderr_text;
};

[[nodiscard]] inline std::string read_all_from_fd(const int fd) {
  std::string output;
  char buffer[4096];
  ssize_t read_count = 0;
  while ((read_count = ::read(fd, buffer, sizeof(buffer))) > 0) {
    output.append(buffer, static_cast<std::size_t>(read_count));
  }
  return output;
}

[[nodiscard]] inline ProcessResult run_process_capture_split(
    const std::vector<std::string>& args,
    const std::filesystem::path& working_directory) {
  if (args.empty()) {
    throw std::runtime_error("binary test support requires a non-empty argv");
  }

  int stdout_pipe[2];
  int stderr_pipe[2];
  if (::pipe(stdout_pipe) != 0 || ::pipe(stderr_pipe) != 0) {
    throw std::runtime_error("binary test support failed to create pipes");
  }

  const pid_t pid = ::fork();
  if (pid < 0) {
    ::close(stdout_pipe[0]);
    ::close(stdout_pipe[1]);
    ::close(stderr_pipe[0]);
    ::close(stderr_pipe[1]);
    throw std::runtime_error("binary test support failed to fork child process");
  }

  if (pid == 0) {
    ::dup2(stdout_pipe[1], STDOUT_FILENO);
    ::dup2(stderr_pipe[1], STDERR_FILENO);
    ::close(stdout_pipe[0]);
    ::close(stdout_pipe[1]);
    ::close(stderr_pipe[0]);
    ::close(stderr_pipe[1]);

    if (::chdir(working_directory.c_str()) != 0) {
      std::perror("chdir");
      std::_Exit(127);
    }

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : args) {
      argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    ::execv(argv.front(), argv.data());
    std::perror("execv");
    std::_Exit(127);
  }

  ::close(stdout_pipe[1]);
  ::close(stderr_pipe[1]);

  ProcessResult result;
  std::thread stdout_reader([&result, fd = stdout_pipe[0]]() {
    result.stdout_text = read_all_from_fd(fd);
    ::close(fd);
  });
  std::thread stderr_reader([&result, fd = stderr_pipe[0]]() {
    result.stderr_text = read_all_from_fd(fd);
    ::close(fd);
  });

  int status = 0;
  if (::waitpid(pid, &status, 0) < 0) {
    throw std::runtime_error("binary test support failed to wait for child process");
  }

  stdout_reader.join();
  stderr_reader.join();

  if (WIFEXITED(status)) {
    result.exit_code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    result.exit_code = 128 + WTERMSIG(status);
  }

  return result;
}

[[nodiscard]] inline std::optional<std::string> extract_json_string_field(
    std::string_view json,
    std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\":";
  const std::size_t key_pos = json.find(needle);
  if (key_pos == std::string_view::npos) {
    return std::nullopt;
  }

  std::size_t value_pos = key_pos + needle.size();
  if (json.substr(value_pos, 4) == "null") {
    return std::nullopt;
  }
  if (value_pos >= json.size() || json[value_pos] != '"') {
    return std::nullopt;
  }

  ++value_pos;
  std::string value;
  bool escaped = false;
  while (value_pos < json.size()) {
    const char current = json[value_pos++];
    if (escaped) {
      value.push_back(current);
      escaped = false;
      continue;
    }
    if (current == '\\') {
      escaped = true;
      continue;
    }
    if (current == '"') {
      return value;
    }
    value.push_back(current);
  }

  return std::nullopt;
}

}  // namespace dasall::tests::integration::access_support