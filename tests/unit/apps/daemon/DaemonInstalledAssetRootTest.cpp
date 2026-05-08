#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

#include "config/InstallLayout.h"
#include "support/TestAssertions.h"

namespace {

namespace fs = std::filesystem;

[[nodiscard]] fs::path repository_root() {
  return fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path().parent_path();
}

[[nodiscard]] fs::path make_temp_directory() {
  const auto unique_suffix = std::to_string(
      std::chrono::high_resolution_clock::now().time_since_epoch().count());
  const auto temp_dir = fs::temp_directory_path() /
                        "dasall-daemon-installed-asset-root-test" / unique_suffix;
  fs::create_directories(temp_dir);
  return temp_dir;
}

class ScopedCurrentPath {
 public:
  explicit ScopedCurrentPath(fs::path next_path)
      : original_path_(fs::current_path()) {
    fs::current_path(std::move(next_path));
  }

  ~ScopedCurrentPath() {
    std::error_code error;
    fs::current_path(original_path_, error);
  }

 private:
  fs::path original_path_;
};

void test_packaged_install_layout_exposes_canonical_paths() {
  using dasall::infra::config::packaged_install_layout;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto layout = packaged_install_layout();
  assert_true(layout.has_consistent_values(),
              "packaged install layout should expose absolute canonical paths");
  assert_equal(std::string("/usr/share/dasall"), layout.readonly_assets_root.string(),
               "packaged install layout should expose canonical readonly assets root");
  assert_equal(std::string("/usr/share/dasall/profiles"), layout.profiles_root.string(),
               "packaged install layout should expose canonical profiles root");
  assert_equal(std::string("/usr/share/dasall/llm/prompts"),
               layout.llm_prompts_root.string(),
               "packaged install layout should expose canonical prompt assets root");
  assert_equal(std::string("/usr/share/dasall/llm/providers"),
               layout.llm_providers_root.string(),
               "packaged install layout should expose canonical provider assets root");
  assert_equal(std::string("/etc/dasall/daemon.json"), layout.daemon_config_path.string(),
               "packaged install layout should expose canonical daemon config path");
  assert_equal(std::string("/run/dasall/daemon.sock"), layout.daemon_socket_path.string(),
               "packaged install layout should expose canonical daemon socket path");
  assert_equal(std::string("/var/lib/dasall"), layout.state_root.string(),
               "packaged install layout should expose canonical state root");
}

void test_resolve_install_layout_is_independent_from_current_working_directory() {
  using dasall::infra::config::packaged_install_layout;
  using dasall::infra::config::resolve_install_layout;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const fs::path temp_root = make_temp_directory();
  const fs::path source_root = fs::weakly_canonical(repository_root());
  const fs::path source_profiles_root = source_root / "profiles";

  const auto from_repository_root = [&]() {
    ScopedCurrentPath cwd(source_root);
    return resolve_install_layout();
  }();
  const auto from_temp_root = [&]() {
    ScopedCurrentPath cwd(temp_root);
    return resolve_install_layout();
  }();

  assert_true(from_repository_root.has_consistent_values() &&
                  from_temp_root.has_consistent_values(),
              "install-aware layout resolution should always return absolute paths");
  assert_equal(from_repository_root.profiles_root.string(),
               from_temp_root.profiles_root.string(),
               "install-aware layout should not change profiles root with cwd");
  assert_equal(from_repository_root.llm_prompts_root.string(),
               from_temp_root.llm_prompts_root.string(),
               "install-aware layout should not change prompt root with cwd");
  assert_equal(from_repository_root.llm_providers_root.string(),
               from_temp_root.llm_providers_root.string(),
               "install-aware layout should not change provider root with cwd");
  assert_equal(from_repository_root.daemon_config_path.string(),
               from_temp_root.daemon_config_path.string(),
               "install-aware layout should keep daemon config path stable");
  assert_equal(from_repository_root.daemon_socket_path.string(),
               from_temp_root.daemon_socket_path.string(),
               "install-aware layout should keep daemon socket path stable");
  assert_true(from_temp_root.profiles_root != temp_root / "profiles",
              "install-aware layout should not derive daemon profiles root from cwd");

  const auto packaged = packaged_install_layout();
  const bool resolved_packaged_root = from_temp_root.profiles_root == packaged.profiles_root;
  const bool resolved_source_root = from_temp_root.profiles_root == source_profiles_root;
  assert_true(resolved_packaged_root || resolved_source_root,
              "install-aware layout should resolve daemon profiles root from either the installed layout or the source tree fallback");
  if (resolved_source_root) {
    assert_equal((source_root / "llm" / "assets" / "prompts").string(),
                 from_temp_root.llm_prompts_root.string(),
                 "source-tree fallback should keep prompt assets rooted under the repository");
    assert_equal((source_root / "llm" / "assets" / "providers").string(),
                 from_temp_root.llm_providers_root.string(),
                 "source-tree fallback should keep provider assets rooted under the repository");
  }

  std::error_code error;
  fs::remove_all(temp_root, error);
}

}  // namespace

int main() {
  try {
    test_packaged_install_layout_exposes_canonical_paths();
    test_resolve_install_layout_is_independent_from_current_working_directory();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}