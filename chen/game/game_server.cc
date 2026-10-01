#include "game_server.h"

#include <cstring>

#include "../log/log.h"

namespace chen::game {

namespace {

constexpr size_t kMaxBodySize = 64 * 1024 * 1024; // 64MB upper bound to avoid OOM

static Logger::ptr logger = LOG_NAME("system");

int readExact(Socket::ptr sock, void* buffer, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        int n = sock->recv(static_cast<char*>(buffer) + offset, length - offset);
        if (n <= 0) {
            return n;
        }
        offset += static_cast<size_t>(n);
    }
    return static_cast<int>(offset);
}
}

class LengthFieldPackageParser : public CustomPackageParser {
public:
    LengthFieldPackageParser(size_t headerSize, uint32_t magic)
        : CustomPackageParser(headerSize, magic) {
        if (headerSize_ < 16) {
            headerSize_ = 16; // ensure we have space for magic/cmd/seq/len
        }
    }

    size_t getHeaderSize() const override { return headerSize_; }

    bool parseHeader(const std::vector<uint8_t>& header, uint32_t& cmdId, uint32_t& seq, size_t& bodyLen) override {
        if (header.size() < headerSize_) {
            return false;
        }
        uint32_t magic = ntohl(*reinterpret_cast<const uint32_t*>(header.data()));
        if (magic_ && magic != magic_) {
            return false;
        }
        cmdId = ntohl(*reinterpret_cast<const uint32_t*>(header.data() + 4));
        seq = ntohl(*reinterpret_cast<const uint32_t*>(header.data() + 8));
        bodyLen = ntohl(*reinterpret_cast<const uint32_t*>(header.data() + 12));
        return true;
    }

    bool validatePackage(const std::vector<uint8_t>& header) override {
        if (header.size() < headerSize_) {
            return false;
        }
        if (!magic_) {
            return true;
        }
        uint32_t magic = ntohl(*reinterpret_cast<const uint32_t*>(header.data()));
        return magic == magic_;
    }

    bool buildPacket(uint32_t cmdId, uint32_t seq, const std::vector<uint8_t>& body, std::vector<uint8_t>& output) override {
        output.resize(headerSize_ + body.size());
        uint8_t* p = output.data();
        uint32_t magic = htonl(magic_);
        uint32_t cmd = htonl(cmdId);
        uint32_t s = htonl(seq);
        uint32_t len = htonl(static_cast<uint32_t>(body.size()));

        std::memcpy(p, &magic, sizeof(magic));
        std::memcpy(p + 4, &cmd, sizeof(cmd));
        std::memcpy(p + 8, &s, sizeof(s));
        std::memcpy(p + 12, &len, sizeof(len));

        if (headerSize_ > 16) {
            std::memset(p + 16, 0, headerSize_ - 16); // padding reserved
        }
        if (!body.empty()) {
            std::memcpy(p + headerSize_, body.data(), body.size());
        }
        return true;
    }
};

IPackageParser::ptr CreateLengthFieldParser(size_t headerSize, uint32_t magic) {
    return IPackageParser::ptr(new LengthFieldPackageParser(headerSize, magic));
}

ConnectionManager::ConnectionManager()
    : nextConnId_(1) {
}

uint32_t ConnectionManager::registerConnection(Socket::ptr socket) {
    if (!socket) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    uint32_t id = nextConnId_++;
    connections_[id] = {socket, nullptr};
    socketToConnId_[socket] = id;
    return id;
}

void ConnectionManager::unregisterConnection(uint32_t connId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = connections_.find(connId);
    if (it != connections_.end()) {
        socketToConnId_.erase(it->second.socket);
        connections_.erase(it);
    }
}

Socket::ptr ConnectionManager::getSocket(uint32_t connId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = connections_.find(connId);
    return it == connections_.end() ? nullptr : it->second.socket;
}

uint32_t ConnectionManager::getConnectionId(Socket::ptr socket) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = socketToConnId_.find(socket);
    return it == socketToConnId_.end() ? 0 : it->second;
}

size_t ConnectionManager::getConnectionCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connections_.size();
}

bool ConnectionManager::sendMessage(uint32_t connId, const std::vector<uint8_t>& data) {
    Socket::ptr sock;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = connections_.find(connId);
        if (it == connections_.end()) {
            return false;
        }
        sock = it->second.socket;
    }
    if (!sock) {
        return false;
    }
    return sock->send(data.data(), data.size()) == static_cast<int>(data.size());
}

size_t ConnectionManager::broadcastMessage(const std::vector<uint32_t>& connIds, const std::vector<uint8_t>& data) {
    size_t ok = 0;
    for (auto id : connIds) {
        if (sendMessage(id, data)) {
            ++ok;
        }
    }
    return ok;
}

void ConnectionManager::setConnectionContext(uint32_t connId, std::shared_ptr<void> context) {
    std::lock_guard<std::mutex> lock(mutex_);
    connections_[connId].context = std::move(context);
}

std::shared_ptr<void> ConnectionManager::getConnectionContext(uint32_t connId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = connections_.find(connId);
    return it == connections_.end() ? nullptr : it->second.context;
}

GenericProtocolServer::GenericProtocolServer(IPackageParser::ptr parser, IOManager* worker, IOManager* io_worker, IOManager* accept_worker)
    : TcpServer(worker, io_worker, accept_worker)
    , packageParser_(parser ? parser : CreateLengthFieldParser())
    , connectionManager_(std::make_shared<ConnectionManager>()) {
    m_type = "game";
}

GenericProtocolServer::~GenericProtocolServer() {
}

void GenericProtocolServer::prepareDispatch() {
    std::lock_guard<std::mutex> lock(handlersMutex_);
    pendingHandlers_.swap(handlers_);
    // handlers_ 现在为空，模块通过 registerHandler 注册到这里
}

void GenericProtocolServer::commitDispatch() {
    std::lock_guard<std::mutex> lock(handlersMutex_);
    // 新 handler 已在 handlers_ 中，释放旧 handler
    pendingHandlers_.clear();
}

void GenericProtocolServer::setPackageParser(IPackageParser::ptr parser) {
    if (parser) {
        packageParser_ = parser;
    }
}

bool GenericProtocolServer::registerHandler(uint32_t cmdId, IMessageHandler::ptr handler) {
    if (!handler) {
        return false;
    }
    std::lock_guard<std::mutex> lock(handlersMutex_);
    return handlers_.emplace(cmdId, handler).second;
}

bool GenericProtocolServer::sendMessage(uint32_t connId, uint32_t cmdId, uint32_t seq, const std::vector<uint8_t>& body) {
    if (!packageParser_) {
        return false;
    }
    std::vector<uint8_t> payload = body;
    if (encodeHook_) {
        if (!encodeHook_(cmdId, seq, payload)) {
            WARN(logger) << "encode hook failed cmd=" << cmdId << " seq=" << seq;
            return false;
        }
    }

    std::vector<uint8_t> packet;
    if (!packageParser_->buildPacket(cmdId, seq, payload, packet)) {
        return false;
    }
    return connectionManager_->sendMessage(connId, packet);
}

size_t GenericProtocolServer::getConnectionCount() const {
    return connectionManager_->getConnectionCount();
}

size_t GenericProtocolServer::getHandlerCount() const {
    std::lock_guard<std::mutex> lock(handlersMutex_);
    return handlers_.size();
}

void GenericProtocolServer::handleClient(Socket::ptr client) {
    uint32_t connId = connectionManager_->registerConnection(client);

    if (connectCallback_) {
        try {
            connectCallback_(connId, client);
        } catch (const std::exception& ex) {
            WARN(logger) << "connect callback exception conn=" << connId << " err=" << ex.what();
        } catch (...) {
            WARN(logger) << "connect callback unknown exception conn=" << connId;
        }
    }

    const size_t headerSize = packageParser_->getHeaderSize();
    std::vector<uint8_t> header(headerSize);

    while (!m_isStop) {
        int n = readExact(client, header.data(), headerSize);
        if (n <= 0) {
            break;
        }

        if (!packageParser_->validatePackage(header)) {
            WARN(logger) << "invalid package header from conn=" << connId;
            break;
        }

        uint32_t cmdId = 0;
        uint32_t seq = 0;
        size_t bodyLen = 0;
        if (!packageParser_->parseHeader(header, cmdId, seq, bodyLen)) {
            WARN(logger) << "parse header failed conn=" << connId;
            break;
        }
        if (bodyLen > kMaxBodySize) {
            WARN(logger) << "body too large bodyLen=" << bodyLen << " conn=" << connId;
            break;
        }

        std::vector<uint8_t> body(bodyLen);
        if (bodyLen > 0) {
            int rn = readExact(client, body.data(), bodyLen);
            if (rn <= 0) {
                break;
            }
        }

        if (decodeHook_) {
            if (!decodeHook_(cmdId, seq, body)) {
                WARN(logger) << "decode hook failed cmd=" << cmdId << " seq=" << seq << " conn=" << connId;
                continue;
            }
        }

        handlePackage(connId, cmdId, seq, header, body);
    }

    if (disconnectCallback_) {
        try {
            disconnectCallback_(connId, client);
        } catch (const std::exception& ex) {
            WARN(logger) << "disconnect callback exception conn=" << connId << " err=" << ex.what();
        } catch (...) {
            WARN(logger) << "disconnect callback unknown exception conn=" << connId;
        }
    }
    connectionManager_->unregisterConnection(connId);
    client->close();
}

void GenericProtocolServer::handlePackage(uint32_t connId, uint32_t cmdId, uint32_t seq
        , const std::vector<uint8_t>& header, const std::vector<uint8_t>& body) {
    IMessageHandler::ptr handler;
    {
        std::lock_guard<std::mutex> lock(handlersMutex_);
        auto it = handlers_.find(cmdId);
        if (it != handlers_.end()) {
            handler = it->second;
        }
    }
    if (!handler) {
        WARN(logger) << "no handler for cmdId=" << cmdId << " conn=" << connId;
        return;
    }
    if (!handler->handle(connId, cmdId, header, body)) {
        WARN(logger) << "handler failed cmdId=" << cmdId << " conn=" << connId << " seq=" << seq;
    }
}

} // namespace chen::game
