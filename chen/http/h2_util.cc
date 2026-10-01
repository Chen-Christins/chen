#include "h2_util.h"

#include <algorithm>
#include <cstring>

namespace chen::http {

nghttp2_nv make_nv(const std::string& name, const std::string& value) {
    nghttp2_nv nv;
    nv.name = (uint8_t*)name.c_str();
    nv.value = (uint8_t*)value.c_str();
    nv.namelen = name.size();
    nv.valuelen = value.size();
    nv.flags = NGHTTP2_NV_FLAG_NONE;
    return nv;
}

nghttp2_nv make_nv(const char* name, const std::string& value) {
    nghttp2_nv nv;
    nv.name = (uint8_t*)name;
    nv.value = (uint8_t*)value.c_str();
    nv.namelen = strlen(name);
    nv.valuelen = value.size();
    nv.flags = NGHTTP2_NV_FLAG_NONE;
    return nv;
}

std::string rcbuf_to_string(nghttp2_rcbuf* buf) {
    auto data = nghttp2_rcbuf_get_buf(buf);
    return std::string((const char*)data.base, data.len);
}

std::string to_lower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

bool is_http1_specific_header(const std::string& name) {
    static const char* h1_headers[] = {
        "connection", "transfer-encoding", "keep-alive",
        "proxy-connection", "upgrade", nullptr
    };
    for (int i = 0; h1_headers[i]; ++i) {
        if (strcasecmp(name.c_str(), h1_headers[i]) == 0) {
            return true;
        }
    }
    return false;
}

std::vector<nghttp2_nv> build_response_nva(int status_code,
    const HttpResponse::MapType& headers,
    const std::vector<std::string>& cookies,
    std::list<std::string>& temp_strs) {

    std::vector<nghttp2_nv> nva;

    // :status pseudo-header — store value in temp_strs to keep alive
    temp_strs.push_back(std::to_string(status_code));
    nva.push_back(make_nv(":status", temp_strs.back()));

    // Regular headers — references to original map data (must stay alive)
    for (auto& h : headers) {
        if (is_http1_specific_header(h.first)) {
            continue;
        }
        // Lowercase the key and store it
        temp_strs.push_back(to_lower(h.first));
        nva.push_back(make_nv(temp_strs.back(), h.second));
    }

    // Set-Cookie headers
    for (auto& c : cookies) {
        nva.push_back(make_nv("set-cookie", c));
    }

    return nva;
}

} // namespace chen::http
