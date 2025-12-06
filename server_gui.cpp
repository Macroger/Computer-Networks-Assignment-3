#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include "shared_state.h"

using namespace ftxui;

int main() {
  auto screen = ScreenInteractive::TerminalOutput();

  // Server stats that will update from shared state
  int messageCount = 0;
  int activeClients = 0;
  int totalReceived = 0;

  // Button actions
  bool running = true;
  auto shutdown_button = Button("Shutdown Server", [&] { 
    g_serverState.serverRunning = false;
    running = false;
    screen.Exit(); 
  });

  auto sidebar = Container::Vertical({
    shutdown_button
  });

  // Create a renderer that continuously updates from shared state
  auto renderer = Renderer(sidebar, [&] {
    // Lock and read from shared state
    {
      std::lock_guard<std::mutex> lock(g_serverState.boardMutex);
      messageCount = g_serverState.messageBoard.size();
      activeClients = g_serverState.activeConnections;
      totalReceived = g_serverState.totalMessagesReceived;
    }

    // Build message board display
    Elements message_elements;
    {
      std::lock_guard<std::mutex> lock(g_serverState.boardMutex);
      
      if (g_serverState.messageBoard.empty()) {
        message_elements.push_back(text("(No messages yet)") | dim);
      } else {
        for (const auto& post : g_serverState.messageBoard) {
          message_elements.push_back(
            vbox({
              text("Author: " + (post.author.empty() ? "(anonymous)" : post.author)) | bold,
              text("Title: " + post.title),
              text("Message: " + post.message),
              separator()
            }) | border
          );
        }
      }
    }

    // Build the layout
    return vbox({
      text("Message Board Server - LIVE") | bold | center | color(Color::Cyan),
      separator(),
      
      // Stats row
      hbox({
        vbox({
          text("Connected Clients") | bold,
          text(std::to_string(activeClients)) | bold | center | color(Color::Green),
        }) | border | flex,
        
        vbox({
          text("Total Messages") | bold,
          text(std::to_string(messageCount)) | bold | center | color(Color::Yellow),
        }) | border | flex,
        
        vbox({
          text("Received") | bold,
          text(std::to_string(totalReceived)) | bold | center | color(Color::Blue),
        }) | border | flex,
      }),
      
      separator(),
      
      // Message board
      vbox({
        text("Message Board") | bold | color(Color::Magenta),
        vbox(message_elements) | border | flex | yscroll,
      }) | flex,
      
      separator(),
      
      // Controls
      vbox({
        text("Controls") | bold,
        sidebar->Render() | border,
      }),
      
      separator(),
      
      text("Updating in real-time from server...") | dim | center,
    }) | border;
  });

  screen.Loop(renderer);
  
  return 0;
}