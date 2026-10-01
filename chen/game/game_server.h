/**
 * @file game_protocol.h
 * @brief 通用游戏应用层协议框架
 * @author Claude
 * @date 2026-02-13
 */
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "../iomanager/iomanager.h"
#include "../socket/socket.h"
#include "../tcp/tcp_server.h"

namespace chen::game {

/**
 * @brief 协议包解析器接口
 * 用于支持不同的协议包格式
 */
class IPackageParser {
public:
    typedef std::shared_ptr<IPackageParser> ptr;
    virtual ~IPackageParser() = default;

    /**
     * @brief 获取包头大小
     */
    virtual size_t getHeaderSize() const = 0;

    /**
     * @brief 解析包头，提取cmdId/seq并返回包体长度
     */
    virtual bool parseHeader(const std::vector<uint8_t>& header, uint32_t& cmdId, uint32_t& seq, size_t& bodyLen) = 0;

    /**
     * @brief 验证包头合法性（含magic校验等）
     */
    virtual bool validatePackage(const std::vector<uint8_t>& header) = 0;

    /**
     * @brief 拼装完整报文（header+body）
     */
    virtual bool buildPacket(uint32_t cmdId, uint32_t seq, const std::vector<uint8_t>& body, std::vector<uint8_t>& output) = 0;
};

/**
 * @brief 自定义协议包解析器基类
 * 用户可以继承此类来实现自己的协议格式
 */
class CustomPackageParser : public IPackageParser {
public:
    /**
     * @brief 构造函数
     * @param headerSize 包头大小
     * @param magic 魔数（可选）
     */
    CustomPackageParser(size_t headerSize, uint32_t magic = 0) : headerSize_(headerSize), magic_(magic) {}

protected:
    size_t headerSize_;
    uint32_t magic_;
};

/**
 * @brief 默认的长度字段解析器创建方法
 * header布局: [magic(uint32)][cmdId(uint32)][seq(uint32)][bodyLen(uint32)] + reserved
 */
IPackageParser::ptr CreateLengthFieldParser(size_t headerSize = 16, uint32_t magic = 0xC0DECAFE);

/**
 * @brief 消息处理器接口
 */
class IMessageHandler {
public:
    typedef std::shared_ptr<IMessageHandler> ptr;
    virtual ~IMessageHandler() = default;

    /**
     * @brief 处理消息
     * @param connId 连接ID
     * @param cmdId 命令ID
     * @param header 包头数据
     * @param body 包体数据
     * @return bool 是否处理成功
     */
    virtual bool handle(uint32_t connId, uint32_t cmdId, const std::vector<uint8_t>& header, const std::vector<uint8_t>& body) = 0;

    /**
     * @brief 获取处理器名称
     * @return std::string 处理器名称
     */
    virtual std::string getName() const = 0;
};

/**
 * @brief 通用消息处理器包装器
 */
template <typename MessageType>
class MessageHandlerWrapper : public IMessageHandler {
public:
    typedef std::shared_ptr<MessageHandlerWrapper> ptr;
    typedef std::function<bool(uint32_t, const MessageType&)> HandlerFunc;

    MessageHandlerWrapper(HandlerFunc func, const std::string& name) 
        : handler_(func), name_(name) {}

    bool handle(uint32_t connId, uint32_t cmdId, const std::vector<uint8_t>& header, const std::vector<uint8_t>& body) override {
        MessageType msg;
        if (!deserialize(body, msg)) {
            return false;
        }
        return handler_(connId, msg);
    }

    std::string getName() const override { return name_; }

private:
    virtual bool deserialize(const std::vector<uint8_t>& data, MessageType& msg) = 0;

protected:
    HandlerFunc handler_;
    std::string name_;
};

/**
 * @brief 连接管理器
 */
class ConnectionManager {
public:
    typedef std::shared_ptr<ConnectionManager> ptr;
    ConnectionManager();

    /**
     * @brief 注册连接
     * @param socket Socket连接
     * @return uint32_t 连接ID
     */
    uint32_t registerConnection(Socket::ptr socket);

    /**
     * @brief 注销连接
     * @param connId 连接ID
     */
    void unregisterConnection(uint32_t connId);

    /**
     * @brief 通过连接ID获取Socket
     * @param connId 连接ID
     * @return Socket::ptr Socket连接
     */
    Socket::ptr getSocket(uint32_t connId);

    /**
     * @brief 通过Socket获取连接ID
     * @param socket Socket连接
     * @return uint32_t 连接ID
     */
    uint32_t getConnectionId(Socket::ptr socket);

    /**
     * @brief 获取连接数量
     * @return size_t 连接数量
     */
    size_t getConnectionCount() const;

    /**
     * @brief 发送消息到指定连接
     * @param connId 连接ID
     * @param data 消息数据
     * @return bool 是否成功
     */
    bool sendMessage(uint32_t connId, const std::vector<uint8_t>& data);

    /**
     * @brief 广播消息到多个连接
     * @param connIds 连接ID列表
     * @param data 消息数据
     * @return size_t 成功发送的数量
     */
    size_t broadcastMessage(const std::vector<uint32_t>& connIds, const std::vector<uint8_t>& data);

    /**
     * @brief 设置连接上下文
     * @param connId 连接ID
     * @param context 上下文数据
     */
    void setConnectionContext(uint32_t connId, std::shared_ptr<void> context);

    /**
     * @brief 获取连接上下文
     * @param connId 连接ID
     * @return std::shared_ptr<void> 上下文数据
     */
    std::shared_ptr<void> getConnectionContext(uint32_t connId);

private:
    struct ConnectionInfo {
        Socket::ptr socket;
        std::shared_ptr<void> context;
    };

    std::map<uint32_t, ConnectionInfo> connections_;
    std::map<Socket::ptr, uint32_t, std::owner_less<Socket::ptr>> socketToConnId_;
    mutable std::mutex mutex_;
    uint32_t nextConnId_;
};

/**
 * @brief 通用协议服务器
 * 继承TcpServer，提供可配置的协议处理能力
 */
class GenericProtocolServer : public TcpServer {
public:
    typedef std::shared_ptr<GenericProtocolServer> ptr;
    using EncodeHook = std::function<bool(uint32_t cmdId, uint32_t seq, std::vector<uint8_t>& data)>;
    using DecodeHook = std::function<bool(uint32_t cmdId, uint32_t seq, std::vector<uint8_t>& data)>;
    using ConnectCallback = std::function<void(uint32_t connId, Socket::ptr client)>;
    using DisconnectCallback = std::function<void(uint32_t connId, Socket::ptr client)>;

    /**
     * @brief 构造函数
     * @param parser 协议解析器
     * @param worker 工作线程池
     * @param io_worker IO工作线程池
     * @param accept_worker 接收连接线程池
     */
    GenericProtocolServer(IPackageParser::ptr parser,
                          IOManager* worker = IOManager::GetThis(),
                          IOManager* io_worker = IOManager::GetThis(), 
                          IOManager* accept_worker = IOManager::GetThis());

    /**
     * @brief 析构函数
     */
    ~GenericProtocolServer() override;

    /**
     * @brief 设置协议解析器
     * @param parser 协议解析器
     */
    void setPackageParser(IPackageParser::ptr parser);

    /**
     * @brief 设置发送前处理钩子（如序列化/加密）
     * @param hook 入参: cmdId/seq/data(可原地改写) 返回false则终止发送
     */
    void setEncodeHook(EncodeHook hook) { encodeHook_ = std::move(hook); }

    /**
     * @brief 设置接收后处理钩子（如解密/反序列化）
     * @param hook 入参: cmdId/seq/data(可原地改写) 返回false则丢弃该包
     */
    void setDecodeHook(DecodeHook hook) { decodeHook_ = std::move(hook); }

    /**
     * @brief 设置连接回调（可在业务层注册）
     * @param callback 入参: connId/client，连接建立时执行
     */
    void setConnectCallback(ConnectCallback callback) { connectCallback_ = std::move(callback); }

    /**
     * @brief 设置断线回调（可在业务层注册）
     * @param callback 入参: connId/client，连接断开时执行
     */
    void setDisconnectCallback(DisconnectCallback callback) { disconnectCallback_ = std::move(callback); }

    /**
     * @brief 注册消息处理器
     * @param cmdId 命令ID
     * @param handler 消息处理器
     * @return bool 是否成功
     */
    bool registerHandler(uint32_t cmdId, IMessageHandler::ptr handler);

    void prepareDispatch() override;
    void commitDispatch() override;

    /**
     * @brief 注册消息处理器（模板方法）
     * @param cmdId 命令ID
     * @param handler 处理函数
     * @param name 处理器名称
     * @return bool 是否成功
     */
    template <typename MessageType>
    bool registerHandler(uint32_t cmdId, std::function<bool(uint32_t, const MessageType&)> handler, const std::string& name = "") {
        // 注意：ProtobufHandler需要用户继承实现
        // 这里提供一个示例注释
        // auto wrapper = std::make_shared<ProtobufHandler<MessageType>>(handler, name);
        // return registerHandler(cmdId, wrapper);
        return false; // 暂时返回false
    }

    /**
     * @brief 发送消息到客户端
     * @param connId 连接ID
     * @param cmdId 命令ID
     * @param seq 序列号
     * @param header 包头数据
     * @param body 包体数据
     * @return bool 是否成功
     */
    bool sendMessage(uint32_t connId, uint32_t cmdId, uint32_t seq, const std::vector<uint8_t>& body);

    /**
     * @brief 发送消息到客户端（模板方法）
     * @param connId 连接ID
     * @param cmdId 命令ID
     * @param seq 序列号
     * @param message 消息对象（需要可序列化）
     * @return bool 是否成功
     */
    template <typename MessageType>
    bool sendMessage(uint32_t connId, uint32_t cmdId, uint32_t seq, const MessageType& message) {
        // 这里需要序列化消息，具体实现取决于序列化方式
        std::vector<uint8_t> data;
        // 序列化代码...
        return sendMessage(connId, cmdId, seq, data);
    }

    /**
     * @brief 获取连接管理器
     * @return ConnectionManager::ptr 连接管理器
     */
    ConnectionManager::ptr getConnectionManager() const { return connectionManager_; }

    /**
     * @brief 获取连接数量
     * @return size_t 连接数量
     */
    size_t getConnectionCount() const;

    /**
     * @brief 获取处理器数量
     * @return size_t 处理器数量
     */
    size_t getHandlerCount() const;

protected:
    /**
     * @brief 处理客户端连接（重写父类方法）
     * @param client 客户端Socket
     */
    void handleClient(Socket::ptr client) override;

private:
    /**
     * @brief 处理协议包
     * @param connId 连接ID
     * @param header 包头数据
     * @param body 包体数据
     */
    void handlePackage(uint32_t connId, uint32_t cmdId, uint32_t seq, const std::vector<uint8_t>& header, const std::vector<uint8_t>& body);

private:
    IPackageParser::ptr packageParser_;
    ConnectionManager::ptr connectionManager_;
    std::map<uint32_t, IMessageHandler::ptr> handlers_;
    /// 热重载时的待提交 handler 表
    std::map<uint32_t, IMessageHandler::ptr> pendingHandlers_;
    mutable std::mutex handlersMutex_;
    EncodeHook encodeHook_;
    DecodeHook decodeHook_;
    ConnectCallback connectCallback_;
    DisconnectCallback disconnectCallback_;
};

/**
 * @brief 便捷的游戏协议服务器类型定义
 * 使用默认的游戏协议包解析器
 */
typedef GenericProtocolServer GameProtocolServer;

/**
 * @brief Protobuf消息处理器
 */
template <typename MessageType>
class ProtobufHandler : public MessageHandlerWrapper<MessageType> {
public:
    typedef std::shared_ptr<ProtobufHandler> ptr;

    ProtobufHandler(typename MessageHandlerWrapper<MessageType>::HandlerFunc func, const std::string& name)
        : MessageHandlerWrapper<MessageType>(func, name) {}

private:
    bool deserialize(const std::vector<uint8_t>& data, MessageType& msg) override {
        // 这里需要实现protobuf反序列化
        // 具体实现取决于protobuf版本和消息类型
        return true; // 暂时返回true
    }
};

} // namespace chen::game
