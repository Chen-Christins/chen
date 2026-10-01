#include "handler_map.h"

namespace chen::rpc {

size_t HandlerKeyHash::operator()(const HandlerKey& key) const {
    if (std::holds_alternative<uint32_t>(key)) {
        return std::hash<uint32_t>{}(std::get<uint32_t>(key));
    }
    return std::hash<std::string>{}(std::get<std::string>(key));
}

} // namespace chen::rpc