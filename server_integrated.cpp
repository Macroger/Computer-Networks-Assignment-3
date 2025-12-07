/*
** Filename: server_integrated.cpp
** Description: Integrated server + GUI application
** Runs the TCP message board server in a background thread
** and displays the FTXUI GUI in the main thread for real-time monitoring
*/

#include <thread>
#include <iostream>
#include "shared_state.h"

// Forward declarations
void run_server();
void run_gui();

int main() {
    std::cout << "Starting Integrated Message Board Server + GUI..." << std::endl;
    
    // Spawn server thread
    std::thread server_thread(run_server);
    server_thread.detach();
    
    // Give server a moment to start
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Run GUI in main thread (FTXUI needs main thread)
    run_gui();
    
    std::cout << "Application shutting down..." << std::endl;
    return 0;
}
