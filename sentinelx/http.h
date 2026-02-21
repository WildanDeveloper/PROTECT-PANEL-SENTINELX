#pragma once
#include <string>
#include <vector>

std::string http_get(const std::string &url,
                     const std::vector<std::string> &headers = {});

std::string http_post(const std::string &url,
                      const std::string &data,
                      const std::vector<std::string> &headers = {});

std::string http_delete(const std::string &url,
                        const std::vector<std::string> &headers = {});
