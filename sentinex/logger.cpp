#include "logger.h"
#include <iostream>
#include <fstream>
#include <mutex>
#include <ctime>
#include <chrono>

static std::mutex  log_mutex;
static std::string log_path;

void log_init(const std::string &lf) { log_path = lf; }

std::string now_str() {
    auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char b[64];
    std::strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    return b;
}

void wlog(const std::string &msg) {
    std::lock_guard<std::mutex> lk(log_mutex);
    std::string line = "[" + now_str() + "] " + msg;
    if (!log_path.empty())
        std::ofstream(log_path, std::ios::app) << line << "\n";
    std::cout << line << "\n";
}
