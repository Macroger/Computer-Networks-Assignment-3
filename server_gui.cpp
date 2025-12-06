#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <vector>
#include <string>

using namespace ftxui;

struct Message {
  std::string author;
  std::string content;
};

int main() {
  auto screen = ScreenInteractive::TerminalOutput();

  // Sample data
  std::vector<Message> messages = {
    {"Alice", "Hello everyone!"},
    {"Bob", "This is a test message."},
    {"Charlie", "FTXUI makes console apps fun!"}
  };

  // Log entries
  std::vector<std::string> logs = {
    "Server started",
    "Client connected: Alice",
    "Message received from Bob"
  };

  // Buttons
  bool running = true;
  auto new_post_button = Button("New Post", [&] { logs.push_back("New post triggered"); });
  auto refresh_button = Button("Refresh", [&] { logs.push_back("Messages refreshed"); });
  auto quit_button = Button("Quit", [&] { logs.push_back("Server shutting down"); running = false; screen.Exit(); });

  auto sidebar = Container::Vertical({
    new_post_button,
    refresh_button,
    quit_button
  });

  auto renderer = Renderer(sidebar, [&] {
    // Render message list
    Elements message_elements;
    for (auto& msg : messages) {
      message_elements.push_back(
        vbox({
          text("Author: " + msg.author) | bold,
          text(msg.content),
          separator()
        }) | border
      );
    }

    // Render logs
    Elements log_elements;
    for (auto& log : logs) {
      log_elements.push_back(text(log));
    }

    return vbox({
      text("Message Board Server") | bold | center,
      separator(),
      hbox({
        vbox(message_elements) | flex,
        sidebar->Render() | border
      }),
      separator(),
      vbox({
        text("Server Log") | bold,
        vbox(log_elements) | border | flex
      }),
      separator(),
      text("Stats: Users=3, Posts=" + std::to_string(messages.size()) + ", Uptime=42s")
    }) | border;
  });

  screen.Loop(renderer);
}