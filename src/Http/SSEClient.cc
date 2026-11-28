#include "SSEClient.hpp"

namespace chen::http {

SSEClient::SSEClient()
    : callback_(nullptr)
{
}

size_t SSEClient::feed(const char* data, size_t len)
{
    if (len == 0) {
        return 0;
    }

    buffer_.append(data, len);
    size_t consumed = len;

    // SSE events are separated by double newline: "\n\n"
    // Lines can end with \n or \r\n, normalize \r\n to \n for simplicity
    size_t pos = 0;
    while (pos < buffer_.size()) {
        // Find end of event: \n\n
        size_t delim = buffer_.find("\n\n", pos);
        if (delim == std::string::npos) {
            break;
        }

        // Extract event text (from pos to delim)
        std::string event_text = buffer_.substr(pos, delim - pos);
        pos = delim + 2; // skip \n\n

        // Parse event fields
        SSEvent event;
        size_t line_start = 0;
        while (line_start < event_text.size()) {
            size_t line_end = event_text.find('\n', line_start);
            if (line_end == std::string::npos) {
                line_end = event_text.size();
            }

            std::string line = event_text.substr(line_start, line_end - line_start);
            line_start = line_end + 1;

            // Remove trailing \r if present (handle \r\n)
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            if (line.empty()) {
                continue;
            }

            // Comment line (starts with ':')
            if (line[0] == ':') {
                continue;
            }

            // Parse field name and value
            size_t colon = line.find(':');
            if (colon == std::string::npos) {
                continue; // Invalid line, skip
            }

            std::string field = line.substr(0, colon);
            std::string value;
            if (colon + 1 < line.size()) {
                // Skip the optional space after colon
                size_t val_start = colon + 1;
                if (line[val_start] == ' ') {
                    val_start++;
                }
                if (val_start < line.size()) {
                    value = line.substr(val_start);
                }
            }

            if (field == "data") {
                if (!event.data.empty()) {
                    event.data += '\n';
                }
                event.data += value;
            } else if (field == "event") {
                event.event = value;
            } else if (field == "id") {
                event.id = value;
            }
            // retry field is ignored in client
        }

        // Trigger callback if event has any content
        if (!event.empty() && callback_) {
            if (!callback_(event)) {
                // Caller requested stop
                consumed = pos;
                // Remove consumed data from buffer
                buffer_.erase(0, pos);
                return consumed;
            }
        }
    }

    // Remove consumed data from buffer
    if (pos > 0) {
        buffer_.erase(0, pos);
    }

    return consumed;
}

void SSEClient::reset()
{
    buffer_.clear();
}

} // namespace chen::http
