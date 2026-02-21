#pragma once
#include <string>

void log_init(const std::string &log_file);
void wlog(const std::string &msg);
std::string now_str();
