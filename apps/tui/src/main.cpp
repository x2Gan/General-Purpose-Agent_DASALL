#include <iostream>

int main() {
#if defined(DASALL_TUI_PROTOTYPE_HAS_FTXUI) && DASALL_TUI_PROTOTYPE_HAS_FTXUI
  constexpr const char* renderer_mode = "ftxui-private-link-ready";
#else
  constexpr const char* renderer_mode = "mock-no-renderer";
#endif

  std::cout << "dasall_tui_prototype: fake-only no-daemon prototype ("
            << renderer_mode
            << ")\n";
  return 0;
}