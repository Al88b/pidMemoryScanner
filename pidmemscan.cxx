#include <iostream>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>

namespace fs = std::filesystem;

// Structure to hold memory region boundaries
struct MemoryRegion {
    unsigned long long start;
    unsigned long long end;
};

// Configures the terminal to read keystrokes immediately without blocking
void set_conio_mode(bool enable) {
    static struct termios oldt, newt;
    if (enable) {
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO); // Disable buffering and local echo
        newt.c_cc[VMIN] = 0;              // Non-blocking read
        newt.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    } else {
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    }
}

// Checks if a specific key (like space bar) was pressed
bool check_keypress(char target_key) {
    char ch;
    if (read(STDIN_FILENO, &ch, 1) > 0) {
        return (ch == target_key);
    }
    return false;
}

// Retrieves all running process IDs from the /proc directory
std::vector<int> get_all_pids() {
    std::vector<int> pids;
    for (const auto& entry : fs::directory_iterator("/proc")) {
        if (entry.is_directory()) {
            std::string name = entry.path().filename().string();
            if (!name.empty() && std::all_of(name.begin(), name.end(), ::isdigit)) {
                pids.push_back(std::stoi(name));
            }
        }
    }
    return pids;
}

// Parses readable memory segments from /proc/[pid]/maps
std::vector<MemoryRegion> get_readable_regions(int pid) {
    std::vector<MemoryRegion> regions;
    std::string maps_path = "/proc/" + std::to_string(pid) + "/maps";
    std::ifstream maps_file(maps_path);
    if (!maps_file.is_open()) return regions;

    std::string line;
    while (std::getline(maps_file, line)) {
        // Look for regions with read ('r') permissions
        size_t space_pos = line.find(' ');
        if (space_pos != std::string::npos && space_pos + 4 < line.length()) {
            std::string perms = line.substr(space_pos + 1, 4);
            if (perms[0] == 'r') { // Ensure it's readable
                unsigned long long start, end;
                if (sscanf(line.c_str(), "%llx-%llx", &start, &end) == 2) {
                    regions.push_back({start, end});
                }
            }
        }
    }
    return regions;
}

// Safely reads and prints a small slice of textual content from a PID's memory
void print_memory_contents(int pid, const std::vector<MemoryRegion>& regions) {
    std::string mem_path = "/proc/" + std::to_string(pid) + "/mem";
    int mem_fd = open(mem_path.c_str(), O_RDONLY);
    if (mem_fd < 0) return;

    std::cout << "\n--- PID [" << pid << "] Memory Dump ---" << std::endl;

    // Limit printing to avoid flooding the screen entirely
    const size_t bytes_to_sample = 64; 
    std::vector<char> buffer(bytes_to_sample + 1);

    for (const auto& region : regions) {
        // Seek to the start of the virtual memory area
        if (lseek64(mem_fd, region.start, SEEK_SET) == -1) continue;

        ssize_t bytes_read = read(mem_fd, buffer.data(), bytes_to_sample);
        if (bytes_read > 0) {
            std::cout << "  Address [0x" << std::hex << region.start << "]: ";
            for (ssize_t i = 0; i < bytes_read; ++i) {
                // Print safe printable ASCII characters or dots for non-printable binary
                if (std::isprint(static_cast<unsigned char>(buffer[i]))) {
                    std::cout << buffer[i];
                } else {
                    std::cout << ".";
                }
            }
            std::cout << std::dec << "\n";
        }
    }
    close(mem_fd);
}

int main() {
    std::cout << "Starting process loop. Press [SPACE] to pause/resume. Press [Ctrl+C] to exit.\n";
    set_conio_mode(true);

    bool paused = false;

    while (true) {
        // Check if space bar (' ') was hit to toggle pause state
        if (check_keypress(' ')) {
            paused = !paused;
            if (paused) {
                std::cout << "\n*** PAUSED. Press [SPACE] again to resume. ***\n" << std::flush;
            } else {
                std::cout << "\n*** RESUMING ***\n" << std::flush;
            }
        }

        if (!paused) {
            std::vector<int> pids = get_all_pids();
            for (int pid : pids) {
                // Periodically check inside the inner loop to maintain responsiveness
                if (check_keypress(' ')) {
                    paused = true;
                    std::cout << "\n*** PAUSED. Press [SPACE] again to resume. ***\n" << std::flush;
                    break;
                }

                std::vector<MemoryRegion> regions = get_readable_regions(pid);
                if (!regions.empty()) {
                    print_memory_contents(pid, regions);
                }
            }
        }

        // Throttle the loop slightly to prevent 100% CPU core exhaustion
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    set_conio_mode(false);
    return 0;
}
