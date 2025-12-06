#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <random>
#include "shared_state.h"

using namespace ftxui;

// Forward declaration - defined in server.cpp
extern void server_run_loop();

int main() {
  // Spawn the server in a background thread
  std::thread server_thread(server_run_loop);
  server_thread.detach();  // Let server run independently
  
  // Give server a moment to start up
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  auto screen = ScreenInteractive::Fullscreen();
  
  // Request animation frame updates to keep rendering fresh
  screen.RequestAnimationFrame();

  // Server stats that will update from shared state
  int messageCount = 0;
  int activeClients = 0;
  int totalReceived = 0;

  // Tab selection state
  int selected_tab = 0;
  
  // Pagination state
  int current_page = 0;
  int current_log_page = 0;
  const int POSTS_PER_PAGE = 7;
  const int EVENTS_PER_PAGE = 7;
  
  // Track new messages for notification banner
  size_t last_displayed_message_count = 0;
  size_t last_displayed_event_count = 0;
  
  // Filter state for Message Board
  std::string filter_title_input_text = "";  // User is typing here
  std::string filter_author_input_text = ""; // User is typing here
  std::string filter_title = "";              // Applied filter
  std::string filter_author = "";             // Applied filter
  bool filter_apply_pending = false;          // Flag to apply filters on next render
  
  // Create input fields for filtering
  auto filter_title_input = Input(&filter_title_input_text, "");
  auto filter_author_input = Input(&filter_author_input_text, "");
  
  // Apply Filters button
  auto apply_filters_button = Button("Apply Filters", [&] {
    filter_apply_pending = true;  // Set flag instead of directly applying
  });
  
  // Clear Filters button
  auto clear_filters_button = Button("Clear Filters", [&] {
    filter_title_input_text = "";
    filter_author_input_text = "";
    filter_title = "";
    filter_author = "";
    current_page = 0;  // Reset to first page
  });

  // Create tab buttons with colors matching their sections
  auto tab_message_board = Button("Message Board", [&] { 
    selected_tab = 0; 
    current_page = 0;  // Reset pagination when switching to Message Board
  });
  auto tab_event_log = Button("Event Log", [&] { 
    selected_tab = 1;
    current_log_page = 0;  // Reset pagination when switching to Event Log
  });
  auto tab_clients = Button("Connected Clients", [&] { selected_tab = 2; });
  auto tab_stats = Button("Stats", [&] { selected_tab = 3; });
  
  auto tab_toggle = Container::Horizontal({
    tab_message_board,
    tab_event_log,
    tab_clients,
    tab_stats
  });

  // Shutdown button
  auto shutdown_button = Button("Shutdown Server", [&] { 
    g_serverState.serverRunning = false;
    screen.Exit(); 
  });
  
  // Jump to Latest button (for Message Board and Event Log)
  auto jump_to_latest_button = Button("Jump to Latest", [&] {
    if (selected_tab == 0) {
      // Message Board
      current_page = 0;
      {
        std::lock_guard<std::mutex> lock(g_serverState.boardMutex);
        last_displayed_message_count = g_serverState.messageBoard.size();
      }
    } else if (selected_tab == 1) {
      // Event Log
      current_log_page = 0;
      {
        std::lock_guard<std::mutex> lock(g_serverState.eventLogMutex);
        last_displayed_event_count = g_serverState.eventLog.size();
      }
    }
  });
  
  // Pagination buttons for Message Board and Event Log
  auto prev_page_button = Button("< Prev", [&] {
    if (selected_tab == 0) {
      if (current_page > 0) current_page--;
    } else if (selected_tab == 1) {
      if (current_log_page > 0) current_log_page--;
    }
  });
  
  auto next_page_button = Button("Next >", [&] {
    if (selected_tab == 0) {
      std::lock_guard<std::mutex> lock(g_serverState.boardMutex);
      int total_posts = g_serverState.messageBoard.size();
      int total_pages = (total_posts + POSTS_PER_PAGE - 1) / POSTS_PER_PAGE;
      if (current_page < total_pages - 1) current_page++;
    } else if (selected_tab == 1) {
      std::lock_guard<std::mutex> lock(g_serverState.eventLogMutex);
      int total_events = g_serverState.eventLog.size();
      int total_pages = (total_events + EVENTS_PER_PAGE - 1) / EVENTS_PER_PAGE;
      if (current_log_page < total_pages - 1) current_log_page++;
    }
  });
  
  // Test button to add random posts
  auto test_posts_button = Button("Add Test Posts", [&] {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> author_dist(0, 9);
    static std::uniform_int_distribution<> title_dist(0, 9);
    static std::uniform_int_distribution<> msg_dist(0, 9);
    
    const std::vector<std::string> authors = {
      "Alice", "Bob", "Charlie", "Diana", "Eve", 
      "Frank", "Grace", "Henry", "Ivy", "Jack"
    };
    const std::vector<std::string> titles = {
      "Hello World", "Testing 123", "Important Update", "Question", "Announcement",
      "News Flash", "Daily Report", "Random Thought", "Discussion", "Information"
    };
    const std::vector<std::string> messages = {
      "This is a test message to check the UI layout.",
      "Lorem ipsum dolor sit amet, consectetur adipiscing elit.",
      "The quick brown fox jumps over the lazy dog.",
      "Testing the message board with random content.",
      "This message was generated for UI testing purposes.",
      "Checking how the interface handles multiple posts.",
      "Random content to fill up the message board.",
      "Another test message with different content.",
      "UI stress test message number X.",
      "Final test message in this batch."
    };
    
    std::lock_guard<std::mutex> lock(g_serverState.boardMutex);
    for (int i = 0; i < 5; i++) {
      Post p;
      p.author = authors[author_dist(gen)];
      p.title = titles[title_dist(gen)];
      p.message = messages[msg_dist(gen)];
      p.clientId = 999; // Test client ID
      g_serverState.messageBoard.push_back(p);
      g_serverState.totalMessagesReceived++;
    }
    g_serverState.logEvent("TEST", "Added 5 random test posts");
  });

  // Main content renderer based on selected tab (with scrolling)
  auto content_scroller = Renderer([&] {
    // Update stats from shared state
    {
      std::lock_guard<std::mutex> lock(g_serverState.boardMutex);
      messageCount = g_serverState.messageBoard.size();
      activeClients = g_serverState.activeConnections;
      totalReceived = g_serverState.totalMessagesReceived;
    }

    // Apply filters if pending
    if (filter_apply_pending) {
      filter_title = filter_title_input_text;
      filter_author = filter_author_input_text;
      current_page = 0;  // Reset to first page when filtering
      filter_apply_pending = false;
    }
    
    // Always check for new messages/events (even when viewing other tabs)
    {
      std::lock_guard<std::mutex> lock(g_serverState.boardMutex);
      if (g_serverState.messageBoard.size() > last_displayed_message_count && current_page > 0) {
        // New messages arrived while viewing older page - will show banner
      }
    }
    
    {
      std::lock_guard<std::mutex> lock(g_serverState.eventLogMutex);
      if (g_serverState.eventLog.size() > last_displayed_event_count && current_log_page > 0) {
        // New events arrived while viewing older page - will show banner
      }
    }
    
    // Update last_displayed_message_count when on page 0
    if (current_page == 0 && selected_tab == 0) {
      std::lock_guard<std::mutex> lock(g_serverState.boardMutex);
      last_displayed_message_count = g_serverState.messageBoard.size();
    }
    
    // Update last_displayed_event_count when on page 0 of Event Log
    if (current_log_page == 0 && selected_tab == 1) {
      std::lock_guard<std::mutex> lock(g_serverState.eventLogMutex);
      last_displayed_event_count = g_serverState.eventLog.size();
    }

    // Viewport content based on selected tab
    Element viewport_content;
    
    // Check for new messages/events for banner display (outside of tab-specific code)
    bool has_new_messages = false;
    bool has_new_events = false;
    
    {
      std::lock_guard<std::mutex> lock(g_serverState.boardMutex);
      has_new_messages = (g_serverState.messageBoard.size() > last_displayed_message_count);
    }
    
    {
      std::lock_guard<std::mutex> lock(g_serverState.eventLogMutex);
      has_new_events = (g_serverState.eventLog.size() > last_displayed_event_count);
    }

    // TAB 0: Message Board
    if (selected_tab == 0) {
      Elements message_elements;
      {
        std::lock_guard<std::mutex> lock(g_serverState.boardMutex);
        
        if (g_serverState.messageBoard.empty()) {
          message_elements.push_back(text("(No messages yet)") | dim);
        } else {
          // New messages check is done above
          
          // Apply filters to create filtered list
          std::vector<int> filtered_indices;
          for (int i = (int)g_serverState.messageBoard.size() - 1; i >= 0; i--) {
            const auto& post = g_serverState.messageBoard[i];
            bool title_match = filter_title.empty() || post.title.find(filter_title) != std::string::npos;
            bool author_match = filter_author.empty() || post.author.find(filter_author) != std::string::npos;
            if (title_match && author_match) {
              filtered_indices.push_back(i);
            }
          }
          
          // Calculate pagination on filtered results
          int total_posts = filtered_indices.size();
          int total_pages = (total_posts + POSTS_PER_PAGE - 1) / POSTS_PER_PAGE;
          
          // Clamp current page to valid range
          if (current_page >= total_pages) {
            current_page = total_pages - 1;
          }
          
          int start_idx = current_page * POSTS_PER_PAGE;
          int end_idx = std::min(start_idx + POSTS_PER_PAGE, total_posts);
          
          // Display posts for current page (newest first)
          for (int i = start_idx; i < end_idx; i++) {
            int original_idx = filtered_indices[i];
            const auto& post = g_serverState.messageBoard[original_idx];
            int post_number = i + 1;  // Posts are 1-indexed for display
            message_elements.push_back(
              vbox(
                hbox(
                  text("#" + std::to_string(post_number) + "  ") | dim,
                  text("Author: " + (post.author.empty() ? "(anonymous)" : post.author)) | bold,
                  text("  |  "),
                  text("Client #" + std::to_string(post.clientId)) | color(Color::Green)
                ),
                text("Title: " + post.title),
                text("Message: " + post.message),
                separator()
              ) | border
            );
          }
        }
      }
      // Build viewport content with optional new messages banner
      Elements viewport_elements;
      viewport_elements.push_back(text("Message Board") | bold | color(Color::Magenta) | center);
      viewport_elements.push_back(separator());
      
      // Show banner if new messages are available and we're not on page 0
      if (has_new_messages && current_page > 0) {
        viewport_elements.push_back(
          text("[!] New messages available") | bold | color(Color::Yellow) | center
        );
        viewport_elements.push_back(separator());
      }
      
      viewport_elements.push_back(
        hbox(
          text("Title: ") | color(Color::Yellow),
          filter_title_input->Render() | flex,
          text("  "),
          text("Author: ") | color(Color::Yellow),
          filter_author_input->Render() | flex
        )
      );
      
      viewport_elements.push_back(
        hbox(
          text("  ") | flex,
          apply_filters_button->Render() | size(WIDTH, GREATER_THAN, 13),
          text("  "),
          clear_filters_button->Render() | size(WIDTH, GREATER_THAN, 13),
          text("  ") | flex
        ) | size(HEIGHT, EQUAL, 3)
      );
      
      viewport_elements.push_back(separator());
      viewport_elements.push_back(text("Page " + std::to_string(current_page + 1) + " of " + std::to_string((g_serverState.messageBoard.size() + POSTS_PER_PAGE - 1) / POSTS_PER_PAGE)) | dim | center);
      viewport_elements.push_back(separator());
      viewport_elements.push_back(vbox(message_elements));
      
      viewport_content = vbox(viewport_elements);
    }

    // TAB 1: Event Log (detailed)
    else if (selected_tab == 1) {
      Elements log_elements;
      {
        std::lock_guard<std::mutex> lock(g_serverState.eventLogMutex);
        if (g_serverState.eventLog.empty()) {
          log_elements.push_back(text("(No events yet)") | dim);
        } else {
          // New events check is done above
          
          // Calculate pagination
          int total_events = g_serverState.eventLog.size();
          int total_pages = (total_events + EVENTS_PER_PAGE - 1) / EVENTS_PER_PAGE;
          
          // Clamp current page to valid range
          if (current_log_page >= total_pages) {
            current_log_page = total_pages - 1;
          }
          
          int start_idx = current_log_page * EVENTS_PER_PAGE;
          int end_idx = std::min(start_idx + EVENTS_PER_PAGE, total_events);
          
          // Display events for current page (most recent first)
          int event_count = 0;
          for (auto it = g_serverState.eventLog.rbegin(); it != g_serverState.eventLog.rend(); ++it) {
            if (event_count >= start_idx && event_count < end_idx) {
              Color event_color = Color::White;
              if (it->event_type == "CONNECT") event_color = Color::Green;
              else if (it->event_type == "DISCONNECT") event_color = Color::Red;
              else if (it->event_type == "POST") event_color = Color::Yellow;
              else if (it->event_type == "GET_BOARD") event_color = Color::Cyan;
              else if (it->event_type == "ERROR") event_color = Color::RedLight;
              
              int event_number = event_count + 1;  // Events are 1-indexed for display
              
              // Show description line with number
              log_elements.push_back(
                hbox(
                  text("#" + std::to_string(event_number) + "  ") | dim,
                  text(it->timestamp) | dim,
                  text(" [" + it->event_type + "] ") | bold | color(event_color),
                  text(it->message)
                )
              );
              
              // If there's a raw message, show it on the next line with indentation
              if (!it->raw_message.empty()) {
                log_elements.push_back(
                  hbox(
                    text("    Raw: "),
                    text(it->raw_message) | color(Color::GrayDark)
                  )
                );
              }
              
              // Add small separator between entries
              log_elements.push_back(text(""));
            }
            event_count++;
          }
        }
      }
      
      // Build viewport content with optional new events banner
      Elements log_viewport_elements;
      log_viewport_elements.push_back(text("Server Event Log - Full Details") | bold | color(Color::Cyan) | center);
      log_viewport_elements.push_back(separator());
      
      // Show banner if new events are available and we're not on page 0
      if (has_new_events && current_log_page > 0) {
        log_viewport_elements.push_back(
          text("[!] New events available") | bold | color(Color::Yellow) | center
        );
        log_viewport_elements.push_back(separator());
      }
      
      log_viewport_elements.push_back(text("Page " + std::to_string(current_log_page + 1) + " of " + std::to_string((g_serverState.eventLog.size() + EVENTS_PER_PAGE - 1) / EVENTS_PER_PAGE)) | dim | center);
      log_viewport_elements.push_back(separator());
      log_viewport_elements.push_back(vbox(log_elements));
      
      viewport_content = vbox(log_viewport_elements);
    }

    // TAB 2: Connected Clients
    else if (selected_tab == 2) {
      Elements client_elements;
      {
        std::lock_guard<std::mutex> lock(g_serverState.clientsMutex);
        
        if (g_serverState.activeClientSockets.empty()) {
          client_elements.push_back(text("(No clients connected)") | dim);
        } else {
          client_elements.push_back(
            text("Active Connections: " + std::to_string(g_serverState.activeClientSockets.size())) | bold | color(Color::Green)
          );
          client_elements.push_back(separator());
          
          for (size_t i = 0; i < g_serverState.activeClientSockets.size(); i++) {
            client_elements.push_back(
              hbox(
                text("  • Socket: ") | bold,
                text(std::to_string(g_serverState.activeClientSockets[i])) | color(Color::Cyan)
              )
            );
          }
        }
      }
      viewport_content = vbox(
        text("Connected Clients") | bold | color(Color::Yellow) | center,
        separator(),
        vbox(client_elements)
      );
    }

    // TAB 3: Stats Overview
    else if (selected_tab == 3) {
      viewport_content = vbox(
        text("Server Statistics") | bold | color(Color::Blue) | center,
        separator(),
        vbox(
          text(""),
          hbox(
            text("  Connected Clients: ") | bold,
            text(std::to_string(activeClients)) | color(Color::Green)
          ),
          text(""),
          hbox(
            text("  Total Messages Posted: ") | bold,
            text(std::to_string(messageCount)) | color(Color::Yellow)
          ),
          text(""),
          hbox(
            text("  Total Requests Received: ") | bold,
            text(std::to_string(totalReceived)) | color(Color::Blue)
          ),
          text("")
        )
      );
    }

    // Bottom panel: Recent TCP alerts (last 5-10 events)
    Elements alert_elements;
    {
      std::lock_guard<std::mutex> lock(g_serverState.eventLogMutex);
      
      if (g_serverState.eventLog.empty()) {
        alert_elements.push_back(text("(No recent events)") | dim);
      } else {
        // Show last 5-10 events (most recent first)
        int count = 0;
        int max_alerts = std::min(10, (int)g_serverState.eventLog.size());
        
        for (auto it = g_serverState.eventLog.rbegin(); it != g_serverState.eventLog.rend() && count < max_alerts; ++it, ++count) {
          Color event_color = Color::White;
          if (it->event_type == "CONNECT") event_color = Color::Green;
          else if (it->event_type == "DISCONNECT") event_color = Color::Red;
          
          alert_elements.push_back(
            hbox(
              text(it->timestamp + " ") | dim,
              text("[" + it->event_type + "] ") | color(event_color) | bold,
              text(it->message)
            )
          );
        }
      }
    }

    // Build complete layout
    return vbox(
      // Header (fixed size)
      vbox(
        text("Message Board Server - LIVE") | bold | center | color(Color::Cyan),
        separator(),
        separator()
      ) | notflex,
      
      // Main viewport (takes remaining space)
      vbox(
        viewport_content | border | size(HEIGHT, LESS_THAN, 40)
      ),
      
      // Bottom sections (fixed size)
      vbox(
        separator(),
        
        // Bottom alert panel (fixed size)
        vbox(
          text("Recent TCP Activity") | bold | center,
          vbox(alert_elements) | border | size(HEIGHT, LESS_THAN, 8)
        ) | size(HEIGHT, LESS_THAN, 10),
        
        separator(),
        
        // Button area (will be rendered separately, fixed size)
        text("")
      ) | notflex
    );
  });

  // Tab bar renderer with colors and minimum widths
  auto tab_bar_component = Renderer(tab_toggle, [&] {
    return hbox(
      text("  "),
      tab_message_board->Render() | color(Color::Magenta) | size(WIDTH, GREATER_THAN, 15),
      text(" "),
      tab_event_log->Render() | color(Color::Cyan) | size(WIDTH, GREATER_THAN, 10),
      text(" "),
      tab_clients->Render() | color(Color::Yellow) | size(WIDTH, GREATER_THAN, 18),
      text(" "),
      tab_stats->Render() | color(Color::Blue) | size(WIDTH, GREATER_THAN, 7),
      text("  ")
    ) | size(HEIGHT, GREATER_THAN, 3);
  });

  // Button renderer - Previous button on left, Next button on right, others in middle
  auto button_component = Renderer(Container::Horizontal({prev_page_button, test_posts_button, jump_to_latest_button, shutdown_button, next_page_button}), [&] {
    return hbox(
      text("  ") | flex,
      prev_page_button->Render() | size(WIDTH, GREATER_THAN, 12) | size(HEIGHT, GREATER_THAN, 3),
      text("  "),
      test_posts_button->Render() | size(WIDTH, GREATER_THAN, 15) | size(HEIGHT, GREATER_THAN, 3),
      text("  "),
      jump_to_latest_button->Render() | size(WIDTH, GREATER_THAN, 14) | size(HEIGHT, GREATER_THAN, 3),
      text("  "),
      shutdown_button->Render() | size(WIDTH, GREATER_THAN, 17) | size(HEIGHT, GREATER_THAN, 3),
      text("  "),
      next_page_button->Render() | size(WIDTH, GREATER_THAN, 8) | size(HEIGHT, GREATER_THAN, 3),
      text("  ") | flex
    );
  });

  // Create filter input renderer - hidden by default (shown inside viewport instead)
  auto filter_inputs_renderer = Renderer(Container::Vertical({
    filter_title_input,
    filter_author_input,
    apply_filters_button,
    clear_filters_button
  }), [&] {
    // Don't render here - these are rendered inside the viewport only
    return text("");
  });

  // Create main component hierarchy
  auto main_component = Container::Vertical({
    tab_bar_component,
    filter_inputs_renderer,
    content_scroller,
    button_component
  });

  screen.Loop(main_component);
  
  return 0;
}
