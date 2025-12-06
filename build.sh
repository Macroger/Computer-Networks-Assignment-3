#!/bin/bash

################################################################################
# Build Script for Message Board Server
################################################################################
#
# DESCRIPTION:
#   Automated compilation script for the TCP Message Board Server project.
#   Handles building the server executable, running unit tests, and cleanup.
#
# USAGE:
#   ./build.sh [option]
#
# OPTIONS:
#   server  - Build the server executable only (default)
#   gui     - Build GUI standalone (experimental)
#   tests   - Build and run the unit test suite
#   all     - Build server, GUI, and tests
#   clean   - Remove all build artifacts and compiled binaries
#   help    - Display this help message
#
# EXAMPLES:
#   ./build.sh                    # Build server executable
#   ./build.sh gui                # Compile and test GUI (experimental)
#   ./build.sh tests              # Compile and run all unit tests
#   ./build.sh all                # Build server, GUI, and tests
#   ./build.sh clean              # Remove build directory
#
# REQUIREMENTS:
#   - g++ compiler (C++17 or later)
#   - Catch2 test framework headers (in tests/catch2/)
#   - Standard POSIX build tools (make, mkdir, rm)
#
# OUTPUT:
#   - Server executable: build/server
#   - GUI executable:    build/server_gui (experimental)
#   - Test executable:  build/server_tests
#   - Colored status messages for easy visibility
#
# NOTES:
#   - All build artifacts are placed in the ./build/ directory
#   - Tests are compiled with -DUNIT_TEST flag to exclude main() from server.cpp
#   - Script exits immediately on any compilation error
#
################################################################################

set -e  # Exit on error

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
CATCH_INCLUDE="${PROJECT_DIR}/tests/catch2"

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Function to print colored messages
print_status() {
    echo -e "${BLUE}[*]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[✓]${NC} $1"
}

print_error() {
    echo -e "${RED}[✗]${NC} $1"
}

# Create build directory if it doesn't exist
mkdir -p "${BUILD_DIR}"

# Build server executable
build_server() {
    print_status "Building server..."
    cd "${PROJECT_DIR}"
    
    g++ -std=c++17 -Wall -Wextra server.cpp -o "${BUILD_DIR}/server"
    
    if [ $? -eq 0 ]; then
        print_success "Server built successfully: ${BUILD_DIR}/server"
    else
        print_error "Failed to build server"
        exit 1
    fi
}

# Build GUI standalone (experimental)
build_gui() {
    print_status "Building GUI (experimental)..."
    cd "${PROJECT_DIR}"
    
    # Use pkg-config to get proper compilation flags for FTXUI
    export PKG_CONFIG_PATH="${HOME}/vcpkg/installed/x64-linux/lib/pkgconfig:$PKG_CONFIG_PATH"
    FTXUI_FLAGS=$(pkg-config --cflags --libs ftxui 2>/dev/null)
    
    if [ -z "$FTXUI_FLAGS" ]; then
        # Fallback to manual flags if pkg-config fails
        VCPKG_DIR="${HOME}/vcpkg"
        FTXUI_INCLUDE="${VCPKG_DIR}/installed/x64-linux/include"
        FTXUI_LIB="${VCPKG_DIR}/installed/x64-linux/lib"
        FTXUI_FLAGS="-I${FTXUI_INCLUDE} -L${FTXUI_LIB} -lftxui-component -lftxui-dom -lftxui-screen"
    fi
    
    # Compile with FTXUI flags and shared state support
    # Only include server.cpp for shared_state.h definition (no main function)
    g++ -std=c++17 -Wall -Wextra -DGUI_BUILD server_gui.cpp server.cpp $FTXUI_FLAGS -o "${BUILD_DIR}/server_gui" 2>&1 | grep -v "undefined reference to .main."
    
    if [ $? -eq 0 ]; then
        print_success "GUI built successfully: ${BUILD_DIR}/server_gui"
    else
        print_error "Failed to build GUI"
        exit 1
    fi
}

# Build and run unit tests
build_tests() {
    print_status "Building unit tests..."
    cd "${PROJECT_DIR}"
    
    g++ -std=c++17 -Wall -Wextra tests/server_test.cpp \
        -I "${CATCH_INCLUDE}" \
        -DUNIT_TEST \
        -o "${BUILD_DIR}/server_tests"
    
    if [ $? -eq 0 ]; then
        print_success "Tests built successfully: ${BUILD_DIR}/server_tests"
        print_status "Running tests..."
        "${BUILD_DIR}/server_tests" -s
    else
        print_error "Failed to build tests"
        exit 1
    fi
}

# Clean build artifacts
clean() {
    print_status "Cleaning build artifacts..."
    rm -rf "${BUILD_DIR}"
    print_success "Cleanup complete"
}

# Show usage
show_usage() {
    echo "Usage: $0 [option]"
    echo "Options:"
    echo "  server  - Build server executable (default)"
    echo "  gui     - Build GUI standalone (experimental)"
    echo "  tests   - Build and run unit tests"
    echo "  all     - Build server, GUI, and tests"
    echo "  clean   - Remove all build artifacts"
    echo ""
    echo "Examples:"
    echo "  ./build.sh           # Build server"
    echo "  ./build.sh gui       # Build GUI (experimental)"
    echo "  ./build.sh tests     # Build and run tests"
    echo "  ./build.sh all       # Build server, GUI, and tests"
}

# Main logic
case "${1:-server}" in
    server)
        build_server
        ;;
    gui)
        build_gui
        ;;
    tests)
        build_tests
        ;;
    all)
        build_server
        build_gui
        build_tests
        ;;
    clean)
        clean
        ;;
    help|-h|--help)
        show_usage
        ;;
    *)
        print_error "Unknown option: $1"
        show_usage
        exit 1
        ;;
esac

print_success "Done!"
