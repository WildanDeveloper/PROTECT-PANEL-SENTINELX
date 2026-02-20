#include "http.h"
#include <curl/curl.h>

static size_t curl_cb(void *c, size_t sz, size_t nm, std::string *s) {
    s->append((char*)c, sz * nm); return sz * nm;
}

static std::string do_req(const std::string &url, const std::string &method,
                           const std::string &data,
                           const std::vector<std::string> &headers) {
    CURL *c = curl_easy_init(); std::string r;
    if (!c) return r;
    struct curl_slist *h = nullptr;
    for (auto &s : headers) h = curl_slist_append(h, s.c_str());
    curl_easy_setopt(c, CURLOPT_URL,            url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER,     h);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION,  curl_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA,      &r);
    curl_easy_setopt(c, CURLOPT_TIMEOUT,        15L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
    if      (method == "POST")   { curl_easy_setopt(c, CURLOPT_POSTFIELDS,     data.c_str()); }
    else if (method == "DELETE") { curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "DELETE"); }
    else                         { curl_easy_setopt(c, CURLOPT_HTTPGET,        1L); }
    curl_easy_perform(c);
    curl_slist_free_all(h); curl_easy_cleanup(c);
    return r;
}

std::string http_get(const std::string &url, const std::vector<std::string> &h)
    { return do_req(url, "GET",    "",   h); }
std::string http_post(const std::string &url, const std::string &d, const std::vector<std::string> &h)
    { return do_req(url, "POST",   d,    h); }
std::string http_delete(const std::string &url, const std::vector<std::string> &h)
    { return do_req(url, "DELETE", "",   h); }
