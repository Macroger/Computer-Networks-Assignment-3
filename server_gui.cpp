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
  // Spawn the server in a background thread so it accepts connections while GUI runs in main thread
  std::thread server_thread(server_run_loop);
  server_thread.detach();  // Let server run independently (we don't need to wait for it)
  
  // Give server a moment to start up and bind to the listening socket
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Create fullscreen interactive screen (FTXUI terminal UI)
  auto screen = ScreenInteractive::Fullscreen();
  
  // Request animation frame updates to keep rendering fresh (60 FPS)
  screen.RequestAnimationFrame();

  // ============================================================================
  // INITIALIZE GUI STATE VARIABLES
  // ============================================================================
  
  // Server stats that will be updated from shared state every frame
  int messageCount = 0;
  int activeClients = 0;
  int totalReceived = 0;

  // Track which tab is currently selected (0=Board, 1=Log, 2=Clients, 3=Stats)
  int selected_tab = 0;
  
  // Pagination state for each tab
  int current_page = 0;          // Current page for Message Board
  int current_log_page = 0;      // Current page for Event Log
  const int POSTS_PER_PAGE = 7;  // 7 messages per page
  const int EVENTS_PER_PAGE = 7; // 7 events per page
  
  // Track how many messages/events the user has seen to detect new content
  size_t last_displayed_message_count = 0; // Count when user last viewed page 0
  size_t last_displayed_event_count = 0;   // Count when user last viewed event page 0
  
  // ============================================================================
  // FILTER STATE (Message Board only)
  // ============================================================================
  
  // Separate input state from applied state to avoid blocking renders
  std::string filter_title_input_text = "";  // What user is typing in title field
  std::string filter_author_input_text = ""; // What user is typing in author field
  std::string filter_title = "";              // Currently applied title filter
  std::string filter_author = "";             // Currently applied author filter
  bool filter_apply_pending = false;          // Flag: apply filters on next render (deferred execution)
  
  // ============================================================================
  // CREATE INPUT COMPONENTS FOR FILTERING
  // ============================================================================
  
  // Create input fields for title and author filtering
  auto filter_title_input = Input(&filter_title_input_text, "");
  auto filter_author_input = Input(&filter_author_input_text, "");
  
  // "Apply Filters" button - sets flag instead of directly modifying filters
  // This deferred approach prevents blocking the render thread
  auto apply_filters_button = Button("Apply Filters", [&] {
    filter_apply_pending = true;  // Set flag, will apply on next render
  });
  
  // "Clear Filters" button - resets both input and applied filter state
  auto clear_filters_button = Button("Clear Filters", [&] {
    filter_title_input_text = "";   // Clear what user typed
    filter_author_input_text = "";
    filter_title = "";              // Clear applied filters
    filter_author = "";
    current_page = 0;               // Reset to first page when clearing filters
  });

  // ============================================================================
  // CREATE TAB SELECTION BUTTONS
  // ============================================================================
  
  // Create button for each tab with corresponding action
  auto tab_message_board = Button("Message Board", [&] { 
    selected_tab = 0; 
    current_page = 0;  // Reset to page 1 when switching tabs
  });
  
  auto tab_event_log = Button("Event Log", [&] { 
    selected_tab = 1;
    current_log_page = 0;  // Reset event log pagination when switching tabs
  });
  
  auto tab_clients = Button("Connected Clients", [&] { 
    selected_tab = 2; 
  });
  
  auto tab_stats = Button("Stats", [&] { 
    selected_tab = 3; 
  });
  
  // Group all tab buttons into a horizontal container for navigation
  auto tab_toggle = Container::Horizontal({
    tab_message_board,
    tab_event_log,
    tab_clients,
    tab_stats
  });

  // ============================================================================
  // CREATE SERVER CONTROL BUTTONS
  // ============================================================================

  // Shutdown button - signals server to stop accepting connections and exit gracefully
  auto shutdown_button = Button("Shutdown Server", [&] { 
    g_serverState.serverRunning = false;  // Signal server thread to stop
    screen.Exit();  // Exit the GUI event loop
  });
  
  // "Jump to Latest" button - resets to page 1 and marks all content as viewed
  // Works for both Message Board and Event Log tabs
  auto jump_to_latest_button = Button("Jump to Latest", [&] {
    if (selected_tab == 0) {
      // For Message Board: go to page 1 and mark all posts as viewed
      current_page = 0;
      {
        std::lock_guard<std::mutex> lock(g_serverState.boardMutex);
        last_displayed_message_count = g_serverState.messageBoard.size();
      }
    } else if (selected_tab == 1) {
      // For Event Log: go to page 1 and mark all events as viewed
      current_log_page = 0;
      {
        std::lock_guard<std::mutex> lock(g_serverState.eventLogMutex);
        last_displayed_event_count = g_serverState.eventLog.size();
      }
    }
  });
  
  // ============================================================================
  // CREATE PAGINATION BUTTONS
  // ============================================================================
  
  // "Previous Page" button - moves backward through pages in current tab
  auto prev_page_button = Button("< Prev", [&] {
    if (selected_tab == 0) {
      // Message Board: decrement page if not on first page
      if (current_page > 0) current_page--;
    } else if (selected_tab == 1) {
      // Event Log: decrement event log page if not on first page
      if (current_log_page > 0) current_log_page--;
    }
  });
  
  // "Next Page" button - moves forward through pages in current tab
  // Also bounds-checks to prevent going past last page
  auto next_page_button = Button("Next >", [&] {
    if (selected_tab == 0) {
      // Message Board: check total pages and increment if not on last page
      std::lock_guard<std::mutex> lock(g_serverState.boardMutex);
      int total_posts = g_serverState.messageBoard.size();
      int total_pages = (total_posts + POSTS_PER_PAGE - 1) / POSTS_PER_PAGE;
      if (current_page < total_pages - 1) current_page++;
    } else if (selected_tab == 1) {
      // Event Log: check total event pages and increment if not on last page
      std::lock_guard<std::mutex> lock(g_serverState.eventLogMutex);
      int total_events = g_serverState.eventLog.size();
      int total_pages = (total_events + EVENTS_PER_PAGE - 1) / EVENTS_PER_PAGE;
      if (current_log_page < total_pages - 1) current_log_page++;
    }
  });
  
  // ============================================================================
  // CREATE TEST DATA GENERATION BUTTON
  // ============================================================================
  
  // "Add Test Posts" button - generates 5 random posts for UI testing
  auto test_posts_button = Button("Add Test Posts", [&] {
    // Initialize random generators (static for persistence across calls)
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> author_dist(0, 9);
    static std::uniform_int_distribution<> title_dist(0, 9);
    static std::uniform_int_distribution<> msg_dist(0, 9);
    
    // Sample data pools for generating realistic-looking posts
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
    
    // Lock the board and add 5 random posts
    std::lock_guard<std::mutex> lock(g_serverState.boardMutex);
    for (int i = 0; i < 5; i++) {
      Post p;
      p.author = authors[author_dist(gen)];
      p.title = titles[title_dist(gen)];
      p.message = messages[msg_dist(gen)];
      p.clientId = 999; // Special ID marking these as test posts
      g_serverState.messageBoard.push_back(p);
      g_serverState.totalMessagesReceived++;
    }
    // Log the test action for visibility in event log
    g_serverState.logEvent("TEST", "Added 5 random test posts");
  });

  // ============================================================================
  // MAIN CONTENT RENDERER (THE HEART OF THE GUI)
  // ============================================================================
  
  // This renderer runs every frame and constructs the main viewport content
  // It handles all tab rendering, filtering, pagination, and state updates
  auto content_scroller = Renderer([&] {
    // Update statistics from shared state (thread-safe with lock)
    {
      std::lock_guard<std::mutex> lock(g_serverState.boardMutex);
      messageCount = g_serverState.messageBoard.size();
      activeClients = g_serverState.activeConnections;
      totalReceived = g_serverState.totalMessagesReceived;
    }

    // Apply any pending filters (set by "Apply Filters" button)
    // This deferred approach prevents blocking the render thread
    if (filter_apply_pending) {
      filter_title = filter_title_input_text;
      filter_author = filter_author_input_text;
      current_page = 0;  // Reset to first page when filters change
      filter_apply_pending = false;
    }
    
    // Check if new messages have arrived (for banner display)
    // This check happens every frame even if not viewing the Message Board
    {
      std::lock_guard<std::mutex> lock(g_serverState.boardMutex);
      if (g_serverState.messageBoard.size() > last_displayed_message_count && current_page > 0) {
        // New messages exist and we're on an older page - banner will display
      }
    }
    
    // Check if new events have arrived (for banner display)
    {
      std::lock_guard<std::mutex> lock(g_serverState.eventLogMutex);
      if (g_serverState.eventLog.size() > last_displayed_event_count && current_log_page > 0) {
        // New events exist and we're on an older event page - banner will display
      }
    }
    
    // Update last displayed message count when viewing page 1 (newest content)
    if (current_page == 0 && selected_tab == 0) {
      std::lock_guard<std::mutex> lock(g_serverState.boardMutex);
      last_displayed_message_count = g_serverState.messageBoard.size();
    }
    
    // Update last displayed event count when viewing page 1 of event log
    if (current_log_page == 0 && selected_tab == 1) {
      std::lock_guard<std::mutex> lock(g_serverState.eventLogMutex);
      last_displayed_event_count = g_serverState.eventLog.size();
    }

    // Determine if there's new content (computed once per frame for all tabs)
    Element viewport_content;
    bool has_new_messages = false;
    bool has_new_events = false;
    
    // Check message board for new content
    {
      std::lock_guard<std::mutex> lock(g_serverState.boardMutex);
      has_new_messages = (g_serverState.messageBoard.size() > last_displayed_message_count);
    }
    
    // Check event log for new content
    {
      std::lock_guard<std::mutex> lock(g_serverState.eventLogMutex);
      has_new_events = (g_serverState.eventLog.size() > last_displayed_event_count);
    }

    // ========================================================================
    // TAB 0: MESSAGE BOARD
    // ========================================================================
    if (selected_tab == 0) {
      Elements message_elements;
      {
        std::lock_guard<std::mutex> lock(g_serverState.boardMutex);
        
        // Handle empty board case
        if (g_serverState.messageBoard.empty()) {
          message_elements.push_back(text("(No messages yet)") | dim);
        } else {
          // BUILD FILTERED MESSAGE LIST
          // Iterate backwards through board (newest first) and collect indices of matching posts
          std::vector<int> filtered_indices;
          for (int i = (int)g_serverState.messageBoard.size() - 1; i >= 0; i--) {
            const auto& post = g_serverState.messageBoard[i];
            // Check if post matches both title and author filters
            bool title_match = filter_title.empty() || post.title.find(filter_title) != std::string::npos;
            bool author_match = filter_author.empty() || post.author.find(filter_author) != std::string::npos;
            if (title_match && author_match) {
              filtered_indices.push_back(i);
            }
          }
          
          // CALCULATE PAGINATION ON FILTERED RESULTS
          int total_posts = filtered_indices.size();
          int total_pages = (total_posts + POSTS_PER_PAGE - 1) / POSTS_PER_PAGE;
          
          // Clamp current page to valid range (handles case where filter reduces results)
          if (current_page >= total_pages) {
            current_page = total_pages - 1;
          }
          
          // Calculate which filtered posts to display on current page
          int start_idx = current_page * POSTS_PER_PAGE;
          int end_idx = std::min(start_idx + POSTS_PER_PAGE, total_posts);
          
          // RENDER EACH POST ON CURRENT PAGE
          for (int i = start_idx; i < end_idx; i++) {
            int original_idx = filtered_indices[i];  // Get index in full board
            const auto& post = g_serverState.messageBoard[original_idx];
            int post_number = i + 1;  // Display numbering (1-indexed)
            
            message_elements.push_back(
              vbox(
                // Post number, author name, and client ID
                hbox(
                  text("#" + std::to_string(post_number) + "  ") | dim,
                  text("Author: " + (post.author.empty() ? "(anonymous)" : post.author)) | bold,
                  text("  |  "),
                  text("Client #" + std::to_string(post.clientId)) | color(Color::Green)
                ),
                // Post title
                text("Title: " + post.title),
                // Post message content
                text("Message: " + post.message),
                separator()
              ) | border
            );
          }
        }
      }
      
      // BUILD VIEWPORT LAYOUT FOR MESSAGE BOARD TAB
      Elements viewport_elements;
      viewport_elements.push_back(text("Message Board") | bold | color(Color::Magenta) | center);
      viewport_elements.push_back(separator());
      
      // Show banner if new messages exist and we're not viewing page 1
      if (has_new_messages && current_page > 0) {
        viewport_elements.push_back(
          text("[!] New messages available") | bold | color(Color::Yellow) | center
        );
        viewport_elements.push_back(separator());
      }
      
      // Filter input fields
      viewport_elements.push_back(
        hbox(
          text("Title: ") | color(Color::Yellow),
          filter_title_input->Render() | flex,
          text("  "),
          text("Author: ") | color(Color::Yellow),
          filter_author_input->Render() | flex
        )
      );
      
      // Filter action buttons
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
      // Page counter
      viewport_elements.push_back(text("Page " + std::to_string(current_page + 1) + " of " + std::to_string((g_serverState.messageBoard.size() + POSTS_PER_PAGE - 1) / POSTS_PER_PAGE)) | dim | center);
      viewport_elements.push_back(separator());
      viewport_elements.push_back(vbox(message_elements));
      
      viewport_content = vbox(viewport_elements);
    }

    // ========================================================================
    // TAB 1: EVENT LOG
    // ========================================================================
    else if (selected_tab == 1) {
      Elements log_elements;
      {
        std::lock_guard<std::mutex> lock(g_serverState.eventLogMutex);
        
        // Handle empty log case
        if (g_serverState.eventLog.empty()) {
          log_elements.push_back(text("(No events yet)") | dim);
        } else {
          // CALCULATE PAGINATION FOR EVENT LOG
          int total_events = g_serverState.eventLog.size();
          int total_pages = (total_events + EVENTS_PER_PAGE - 1) / EVENTS_PER_PAGE;
          
          // Clamp current log page to valid range
          if (current_log_page >= total_pages) {
            current_log_page = total_pages - 1;
          }
          
          // Calculate which events to display on current page
          int start_idx = current_log_page * EVENTS_PER_PAGE;
          int end_idx = std::min(start_idx + EVENTS_PER_PAGE, total_events);
          
          // RENDER EVENTS (newest first, using reverse iterator)
          int event_count = 0;
          for (auto it = g_serverState.eventLog.rbegin(); it != g_serverState.eventLog.rend(); ++it) {
            // Only render events within current page range
            if (event_count >= start_idx && event_count < end_idx) {
              // Select color based on event type
              Color event_color = Color::White;
              if (it->event_type == "CONNECT") event_color = Color::Green;
              else if (it->event_type == "DISCONNECT") event_color = Color::Red;
              else if (it->event_type == "POST") event_color = Color::Yellow;
              else if (it->event_type == "GET_BOARD") event_color = Color::Cyan;
              else if (it->event_type == "ERROR") event_color = Color::RedLight;
              
              int event_number = event_count + 1;  // Display numbering (1-indexed)
              
              // Event header line: number, timestamp, type, message
              log_elements.push_back(
                hbox(
                  text("#" + std::to_string(event_number) + "  ") | dim,
                  text(it->timestamp) | dim,
                  text(" [" + it->event_type + "] ") | bold | color(event_color),
                  text(it->message)
                )
              );
              
              // If event has raw wire-format message, display it below
              if (!it->raw_message.empty()) {
                log_elements.push_back(
                  hbox(
                    text("    Raw: "),
                    text(it->raw_message) | color(Color::GrayDark)
                  )
                );
              }
              
              // Small separator between events
              log_elements.push_back(text(""));
            }
            event_count++;
          }
        }
      }
      
      // BUILD VIEWPORT LAYOUT FOR EVENT LOG TAB
      Elements log_viewport_elements;
      log_viewport_elements.push_back(text("Server Event Log - Full Details") | bold | color(Color::Cyan) | center);
      log_viewport_elements.push_back(separator());
      
      // Show banner if new events exist and we're not viewing page 1
      if (has_new_events && current_log_page > 0) {
        log_viewport_elements.push_back(
          text("[!] New events available") | bold | color(Color::Yellow) | center
        );
        log_viewport_elements.push_back(separator());
      }
      
      // Page counter for event log
      log_viewport_elements.push_back(text("Page " + std::to_string(current_log_page + 1) + " of " + std::to_string((g_serverState.eventLog.size() + EVENTS_PER_PAGE - 1) / EVENTS_PER_PAGE)) | dim | center);
      log_viewport_elements.push_back(separator());
      log_viewport_elements.push_back(vbox(log_elements));
      
      viewport_content = vbox(log_viewport_elements);
    }

    // ========================================================================
    // TAB 2: CONNECTED CLIENTS
    // ========================================================================
    else if (selected_tab == 2) {
      Elements client_elements;
      {
        std::lock_guard<std::mutex> lock(g_serverState.clientsMutex);
        
        // Handle no connected clients case
        if (g_serverState.activeClientSockets.empty()) {
          client_elements.push_back(text("(No clients connected)") | dim);
        } else {
          // Display count of active connections
          client_elements.push_back(
            text("Active Connections: " + std::to_string(g_serverState.activeClientSockets.size())) | bold | color(Color::Green)
          );
          client_elements.push_back(separator());
          
          // List each connected client's socket
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

    // ========================================================================
    // TAB 3: SERVER STATISTICS
    // ========================================================================
    else if (selected_tab == 3) {
      viewport_content = vbox(
        text("Server Statistics") | bold | color(Color::Blue) | center,
        separator(),
        vbox(
          text(""),
          // Active connections counter
          hbox(
            text("  Connected Clients: ") | bold,
            text(std::to_string(activeClients)) | color(Color::Green)
          ),
          text(""),
          // Total messages posted
          hbox(
            text("  Total Messages Posted: ") | bold,
            text(std::to_string(messageCount)) | color(Color::Yellow)
          ),
          text(""),
          // Total requests received
          hbox(
            text("  Total Requests Received: ") | bold,
            text(std::to_string(totalReceived)) | color(Color::Blue)
          ),
          text("")
        )
      );
    }

    // ========================================================================
    // BOTTOM PANEL: RECENT TCP ACTIVITY LOG
    // ========================================================================
    
    Elements alert_elements;
    {
      std::lock_guard<std::mutex> lock(g_serverState.eventLogMutex);
      
      // Handle empty event log case
      if (g_serverState.eventLog.empty()) {
        alert_elements.push_back(text("(No recent events)") | dim);
      } else {
        // Show last 8-10 events (most recent first)
        int count = 0;
        int max_alerts = std::min(10, (int)g_serverState.eventLog.size());
        
        // Iterate backwards through event log (newest first)
        for (auto it = g_serverState.eventLog.rbegin(); it != g_serverState.eventLog.rend() && count < max_alerts; ++it, ++count) {
          // Color code based on event type
          Color event_color = Color::White;
          if (it->event_type == "CONNECT") event_color = Color::Green;
          else if (it->event_type == "DISCONNECT") event_color = Color::Red;
          
          // Format: timestamp [type] message
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

    // ========================================================================
    // ASSEMBLE COMPLETE SCREEN LAYOUT
    // ========================================================================
    
    return vbox(
      // Header (fixed size - doesn't flex)
      vbox(
        text("Message Board Server - LIVE") | bold | center | color(Color::Cyan),
        separator(),
        separator()
      ) | notflex,
      
      // Main viewport (takes up middle space)
      vbox(
        viewport_content | border | size(HEIGHT, LESS_THAN, 40)
      ),
      
      // Bottom sections (fixed size - doesn't flex)
      vbox(
        separator(),
        
        // Recent TCP activity panel (fixed size)
        vbox(
          text("Recent TCP Activity") | bold | center,
          vbox(alert_elements) | border | size(HEIGHT, LESS_THAN, 8)
        ) | size(HEIGHT, LESS_THAN, 10),
        
        separator(),
        
        // Placeholder for button area
        text("")
      ) | notflex
    );
  });

  // ============================================================================
  // TAB BAR RENDERER (TOP OF SCREEN)
  // ============================================================================
  
  // Create visual tab bar with all 4 tabs, each color-coded and sized appropriately
  // This renderer displays the tab buttons at the top of the screen
  auto tab_bar_component = Renderer(tab_toggle, [&] {
    return hbox(
      text("  "),
      // Message Board tab - Magenta color, sized to fit text
      tab_message_board->Render() | color(Color::Magenta) | size(WIDTH, GREATER_THAN, 15),
      text(" "),
      // Event Log tab - Cyan color
      tab_event_log->Render() | color(Color::Cyan) | size(WIDTH, GREATER_THAN, 10),
      text(" "),
      // Connected Clients tab - Yellow color
      tab_clients->Render() | color(Color::Yellow) | size(WIDTH, GREATER_THAN, 18),
      text(" "),
      // Server Statistics tab - Blue color
      tab_stats->Render() | color(Color::Blue) | size(WIDTH, GREATER_THAN, 7),
      text("  ")
    ) | size(HEIGHT, GREATER_THAN, 3);  // Tab bar always 3+ lines tall
  });

  // ============================================================================
  // BOTTOM BUTTON BAR RENDERER
  // ============================================================================
  
  // Create bottom control panel with all navigation and server control buttons
  // Previous page button on left, pagination/control buttons in middle, Next page button on right
  // This allows users to navigate between pages and control the server
  auto button_component = Renderer(Container::Horizontal({prev_page_button, test_posts_button, jump_to_latest_button, shutdown_button, next_page_button}), [&] {
    return hbox(
      // Left padding with flex to push buttons inward from edges
      text("  ") | flex,
      
      // Previous page button (left-aligned)
      prev_page_button->Render() | size(WIDTH, GREATER_THAN, 12) | size(HEIGHT, GREATER_THAN, 3),
      text("  "),
      
      // Test data generation button
      test_posts_button->Render() | size(WIDTH, GREATER_THAN, 15) | size(HEIGHT, GREATER_THAN, 3),
      text("  "),
      
      // Jump to latest content button
      jump_to_latest_button->Render() | size(WIDTH, GREATER_THAN, 14) | size(HEIGHT, GREATER_THAN, 3),
      text("  "),
      
      // Shutdown server button
      shutdown_button->Render() | size(WIDTH, GREATER_THAN, 17) | size(HEIGHT, GREATER_THAN, 3),
      text("  "),
      
      // Next page button (right-aligned)
      next_page_button->Render() | size(WIDTH, GREATER_THAN, 8) | size(HEIGHT, GREATER_THAN, 3),
      
      // Right padding with flex
      text("  ") | flex
    );
  });

  // ============================================================================
  // FILTER INPUT RENDERER (HIDDEN - ONLY RENDERED IN VIEWPORT)
  // ============================================================================
  
  // Create renderer for filter inputs, but keep it hidden here (only shows inside main viewport)
  // This prevents duplicate filter fields appearing at top of screen while maintaining focus handling
  auto filter_inputs_renderer = Renderer(Container::Vertical({
    filter_title_input,
    filter_author_input,
    apply_filters_button,
    clear_filters_button
  }), [&] {
    // Return empty element - filters are rendered inside the viewport, not here
    // This trick keeps the components in the container hierarchy for focus/input handling
    // while preventing them from being visible in the layout
    return text("");
  });

  // ============================================================================
  // MAIN SCREEN LAYOUT ASSEMBLY
  // ============================================================================
  
  // Assemble complete screen: tab bar at top, content in middle, buttons at bottom
  // This vertical arrangement creates the full screen layout
  auto main_component = Container::Vertical({
    tab_bar_component,      // Tab selection bar (Magenta, Cyan, Yellow, Blue)
    filter_inputs_renderer, // Hidden filter inputs (for focus/navigation only)
    content_scroller,       // Main viewport with tab-specific content (takes most space)
    button_component        // Button bar with navigation and server controls
  });

  // ============================================================================
  // SCREEN EVENT LOOP - START GUI
  // ============================================================================
  
  // Start the fullscreen event loop - this blocks until user exits (Ctrl+C or Shutdown button)
  // The screen will render at ~60 FPS, handle all user input events, and update state
  screen.Loop(main_component);
  
  // ============================================================================
  // CLEANUP AND EXIT
  // ============================================================================
  
  // Signal the background server thread to stop accepting connections and shut down
  g_serverState.serverRunning = false;

  // Give the server thread time to finish cleanup (close sockets, close listening socket, etc)
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Exit successfully
  return 0;
}
