/**
 * @file h2_util.h
 * @brief HTTP/2 工具函数
 */
#pragma once

#include <list>
#include <string>
#include <vector>

#include <nghttp2/nghttp2.h>

#include "http.h"

namespace chen::http {

// Make an nghttp2_nv from name/value strings.
// The returned nv references the string data — caller must keep strings alive.
nghttp2_nv make_nv(const std::string& name, const std::string& value);

// Make an nghttp2_nv from C-string name and std::string value.
nghttp2_nv make_nv(const char* name, const std::string& value);

// Convert nghttp2_rcbuf to std::string.
std::string rcbuf_to_string(nghttp2_rcbuf* buf);

// Convert string to lowercase (for HTTP/2 header names).
std::string to_lower(const std::string& s);

// Build nghttp2_nv array from HttpResponse for nghttp2_submit_response.
// Handles :status pseudo-header, skips HTTP/1.1-specific headers, lowercases keys.
// temp_strs receives temporary strings that must outlive the returned nva.
std::vector<nghttp2_nv> build_response_nva(int status_code, const HttpResponse::MapType& headers,
                                           const std::vector<std::string>& cookies, std::list<std::string>& temp_strs);

// Check if a header name is a connection-specific header (should be skipped in HTTP/2).
bool is_http1_specific_header(const std::string& name);

} // namespace chen::http
