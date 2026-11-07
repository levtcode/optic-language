/**
 * @file cli_component.cpp
 * @brief Integration fixture for argument boundaries, streams and process status.
 */
#include <csignal>
#include <cstdlib>
#include <iostream>

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::cout << argv[i] << '\0';
    }
    std::cout.flush();
    std::cerr << "component stderr\n";
#ifndef _WIN32
    if (std::getenv("OPTIC_TEST_SIGNAL")) std::raise(SIGTERM);
#endif
    if (const char* status = std::getenv("OPTIC_TEST_EXIT")) return std::atoi(status);
    return 0;
}
