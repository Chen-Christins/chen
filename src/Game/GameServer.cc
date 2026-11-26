#include "GameServer.hpp"

#include <cstring>

#include "../Log/Log.hpp"

namespace chen::game {

CustomPackageParser::CustomPackageParser(size_t header_size, uint32_t magic)
    : header_size_(header_size)
    , magic_(magic)
{
}

namespace {

constexpr size_t kMaxBodySize = 64 * 1024 * 1024; // 64MB upper bound to avoid OOM

static Logger::ptr logger = LOG_NAME("system");

int readExact(Socket::ptr sock, void* buffer, size_t length)
{
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
} // namespace

class LengthFieldPackageParser : public CustomPackageParser {
public:
    LengthFieldPackageParser(size_t header_size, uint32_t magic)
        : CustomPackageParser(header_size, magic)
    {
        if (header_size_ < 16) {
            header_size_ = 16; // ensure we have space for magic/cmd/seq/len
        }
    }

    size_t getHeaderSize() const override { return header_size_; }

    bool parseHeader(const std::vector<uint8_t>& header, uint32_t& cmd_id, uint32_t& seq, size_t& body_len) override
    {
        if (header.size() < header_size_) {
            return false;
        }
        uint32_t magic = ntohl(*reinterpret_cast<const uint32_t*>(header.data()));
        if (magic_ && magic != magic_) {
            return false;
        }
        cmd_id = ntohl(*reinterpret_cast<const uint32_t*>(header.data() + 4));
        seq = ntohl(*reinterpret_cast<const uint32_t*>(header.data() + 8));
        body_len = ntohl(*reinterpret_cast<const uint32_t*>(header.data() + 12));
        return true;
    }

    bool validatePackage(const std::vector<uint8_t>& header) override
    {
        if (header.size() < header_size_) {
            return false;
        }
        if (!magic_) {
            return true;
        }
        uint32_t magic = ntohl(*reinterpret_cast<const uint32_t*>(header.data()));
        return magic == magic_;
    }

    bool buildPacket(uint32_t cmd_id,
        uint32_t seq,
        const std::vector<uint8_t>& body,
        std::vector<uint8_t>& output) override
    {
        output.resize(header_size_ + body.size());
        uint8_t* p = output.data();
        uint32_t magic = htonl(magic_);
        uint32_t cmd = htonl(cmd_id);
        uint32_t s = htonl(seq);
        uint32_t len = htonl(static_cast<uint32_t>(body.size()));

        std::memcpy(p, &magic, sizeof(magic));
        std::memcpy(p + 4, &cmd, sizeof(cmd));
        std::memcpy(p + 8, &s, sizeof(s));
        std::memcpy(p + 12, &len, sizeof(len));

        if (header_size_ > 16) {
            std::memset(p + 16, 0, header_size_ - 16); // padding reserved
        }
        if (!body.empty()) {
            std::memcpy(p + header_size_, body.data(), body.size());
        }
        return true;
    }
};

IPackageParser::ptr CreateLengthFieldParser(size_t header_size, uint32_t magic)
{
    return IPackageParser::ptr(new LengthFieldPackageParser(header_size, magic));
}

ConnectionManager::ConnectionManager()
    : next_conn_id_(1)
{
}

uint32_t ConnectionManager::registerConnection(Socket::ptr socket)
{
    if (!socket) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    uint32_t id = next_conn_id_++;
    connections_[id] = {socket, nullptr};
    socket_to_conn_id_[socket] = id;
    return id;
}

void ConnectionManager::unregisterConnection(uint32_t conn_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = connections_.find(conn_id);
    if (it != connections_.end()) {
        socket_to_conn_id_.erase(it->second.socket);
        connections_.erase(it);
    }
}

Socket::ptr ConnectionManager::getSocket(uint32_t conn_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = connections_.find(conn_id);
    return it == connections_.end() ? nullptr : it->second.socket;
}

uint32_t ConnectionManager::getConnectionId(Socket::ptr socket)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = socket_to_conn_id_.find(socket);
    return it == socket_to_conn_id_.end() ? 0 : it->second;
}

size_t ConnectionManager::getConnectionCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return connections_.size();
}

bool ConnectionManager::sendMessage(uint32_t conn_id, const std::vector<uint8_t>& data)
{
    Socket::ptr sock;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = connections_.find(conn_id);
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

size_t ConnectionManager::broadcastMessage(const std::vector<uint32_t>& conn_ids, const std::vector<uint8_t>& data)
{
    size_t ok = 0;
    for (auto id : conn_ids) {
        if (sendMessage(id, data)) {
            ++ok;
        }
    }
    return ok;
}

void ConnectionManager::setConnectionContext(uint32_t conn_id, std::shared_ptr<void> context)
{
    std::lock_guard<std::mutex> lock(mutex_);
    connections_[conn_id].context = std::move(context);
}

std::shared_ptr<void> ConnectionManager::getConnectionContext(uint32_t conn_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = connections_.find(conn_id);
    return it == connections_.end() ? nullptr : it->second.context;
}

GenericProtocolServer::GenericProtocolServer(IPackageParser::ptr parser, IOManager* worker, IOManager* io_worker, IOManager* accept_worker)
    : TcpServer(worker, io_worker, accept_worker)
    , package_parser_(parser ? parser : CreateLengthFieldParser())
    , connection_manager_(std::make_shared<ConnectionManager>())
{
    type_ = "game";
}

GenericProtocolServer::~GenericProtocolServer()
{
}

void GenericProtocolServer::setPackageParser(IPackageParser::ptr parser)
{
    if (parser) {
        package_parser_ = parser;
    }
}

bool GenericProtocolServer::registerHandler(uint32_t cmd_id, IMessageHandler::ptr handler)
{
    if (!handler) {
        return false;
    }
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    return handlers_.emplace(cmd_id, handler).second;
}

bool GenericProtocolServer::sendMessage(uint32_t conn_id,
    uint32_t cmd_id,
    uint32_t seq,
    const std::vector<uint8_t>& body)
{
    if (!package_parser_) {
        return false;
    }
    std::vector<uint8_t> payload = body;
    if (encode_hook_) {
        if (!encode_hook_(cmd_id, seq, payload)) {
            WARN(logger) << "encode hook failed cmd=" << cmd_id << " seq=" << seq;
            return false;
        }
    }

    std::vector<uint8_t> packet;
    if (!package_parser_->buildPacket(cmd_id, seq, payload, packet)) {
        return false;
    }
    return connection_manager_->sendMessage(conn_id, packet);
}

size_t GenericProtocolServer::getConnectionCount() const
{
    return connection_manager_->getConnectionCount();
}

size_t GenericProtocolServer::getHandlerCount() const
{
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    return handlers_.size();
}

void GenericProtocolServer::handleClient(Socket::ptr client)
{
    uint32_t conn_id = connection_manager_->registerConnection(client);

    if (connect_callback_) {
        try {
            connect_callback_(conn_id, client);
        } catch (const std::exception& ex) {
            WARN(logger) << "connect callback exception conn=" << conn_id << " err=" << ex.what();
        } catch (...) {
            WARN(logger) << "connect callback unknown exception conn=" << conn_id;
        }
    }

    const size_t header_size = package_parser_->getHeaderSize();
    std::vector<uint8_t> header(header_size);

    while (!is_stop_ && !isDraining()) {
        int n = readExact(client, header.data(), header_size);
        if (n <= 0) {
            break;
        }

        if (!package_parser_->validatePackage(header)) {
            WARN(logger) << "invalid package header from conn=" << conn_id;
            break;
        }

        uint32_t cmd_id = 0;
        uint32_t seq = 0;
        size_t body_len = 0;
        if (!package_parser_->parseHeader(header, cmd_id, seq, body_len)) {
            WARN(logger) << "parse header failed conn=" << conn_id;
            break;
        }
        if (body_len > kMaxBodySize) {
            WARN(logger) << "body too large bodyLen=" << body_len << " conn=" << conn_id;
            break;
        }

        std::vector<uint8_t> body(body_len);
        if (body_len > 0) {
            int rn = readExact(client, body.data(), body_len);
            if (rn <= 0) {
                break;
            }
        }

        if (decode_hook_) {
            if (!decode_hook_(cmd_id, seq, body)) {
                WARN(logger) << "decode hook failed cmd=" << cmd_id << " seq=" << seq << " conn=" << conn_id;
                continue;
            }
        }

        {
            // 一条协议包的处理（含回包）就是 drain 的请求边界
            RequestGuard guard(this, client);
            handlePackage(conn_id, cmd_id, seq, header, body);
        }
    }

    if (disconnect_callback_) {
        try {
            disconnect_callback_(conn_id, client);
        } catch (const std::exception& ex) {
            WARN(logger) << "disconnect callback exception conn=" << conn_id << " err=" << ex.what();
        } catch (...) {
            WARN(logger) << "disconnect callback unknown exception conn=" << conn_id;
        }
    }
    connection_manager_->unregisterConnection(conn_id);
    client->close();
}

void GenericProtocolServer::handlePackage(uint32_t conn_id,
    uint32_t cmd_id,
    uint32_t seq,
    const std::vector<uint8_t>& header,
    const std::vector<uint8_t>& body)
{
    IMessageHandler::ptr handler;
    {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        auto it = handlers_.find(cmd_id);
        if (it != handlers_.end()) {
            handler = it->second;
        }
    }
    if (!handler) {
        WARN(logger) << "no handler for cmdId=" << cmd_id << " conn=" << conn_id;
        return;
    }
    if (!handler->handle(conn_id, cmd_id, header, body)) {
        WARN(logger) << "handler failed cmdId=" << cmd_id << " conn=" << conn_id << " seq=" << seq;
    }
}

} // namespace chen::game
