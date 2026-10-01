/**
 * @file protocol.h
 * @brief RPC协议定义
 * @author Christins
 * @date 2025-12-03
 * @copyright GPL-3.0
 */
#pragma once

#include <memory>
#include <string>

#include "../bytearray/bytearray.h"

namespace chen::rpc {

// 协议魔数
constexpr uint8_t RPC_PROTOCOL_MAGIC = 0xCC;
// 协议版本号
constexpr uint8_t RPC_PROTOCOL_VERSION = 0x03;

/**
 * @brief RPC 消息类型
 */
enum class MessageType : uint8_t {
    REQUEST = 0,   ///< 请求
    RESPONSE = 1,  ///< 响应
    HEARTBEAT = 2, ///< 心跳
    NOTIFY = 3,    ///< 单向通知（fire-and-forget，无回包）
};

/**
 * @brief 中心转发（Hub）路由方式
 * @details 普通点对点 RPC 为 NONE，路由字段全部为 0。
 *          hub 收到请求后按路由方式选择目标连接转发并中继回包。
 */
enum class RoutingMethod : uint8_t {
    NONE = 0,      ///< 普通 RPC，无路由
    DIRECT = 1,    ///< 按目标 peer_id 直发
    GROUPID = 2,   ///< 按 (func_id, group_id) 拓扑选实例
    BROADCAST = 3, ///< 广播到 func_id 全部实例（单向）
    BIND_ID = 4,   ///< 按 (func_id, bind_id) 绑定关系路由
};

#pragma pack(push, 1)
/**
 * @brief 固定长度的传输层帧头
 *
 * 独立为 packed 结构体，确保 sizeof 即为线缆上的实际字节数。
 * BASE_LENGTH 由此推导，与字段定义始终保持一致。
 */
struct ProtocolHeader {
    uint8_t magic = RPC_PROTOCOL_MAGIC;
    uint8_t version = RPC_PROTOCOL_VERSION;
    MessageType type = MessageType::REQUEST;
    uint8_t routing = 0;     ///< RoutingMethod
    uint8_t real_random = 0; ///< GROUPID: 固定拓扑(0) / 动态拓扑(1)
    uint32_t sequence = 0;
    uint32_t length = 0;
    uint32_t cmd = 0;         ///< CmdID：0=按 body 方法名分派（兼容），非 0=按 CmdID 分派（body 直接为 args）
    uint32_t src_peer_id = 0; /// 来源对端身份（服务连接注册的 peer_id）
    uint32_t dst_peer_id = 0; /// DIRECT 目标对端身份
    uint32_t func_id = 0;     /// GROUPID / BROADCAST / BIND_ID 目标功能类型
    uint32_t group_id = 0;    /// GROUPID 分片键
    uint32_t bind_id = 0;     /// BIND_ID 绑定业务 ID
    uint32_t region = 0;      /// 预留：来源区号（本区恒 0，跨区路由二期使用）
};
#pragma pack(pop)

/**
 * @brief RPC 传输协议帧
 *
 * 继承 ProtocolHeader 的 5 个固定字段 + 变长 body。
 * Encode/Decode 逐一读写字段，不依赖内存布局，
 * 因此 BASE_LENGTH 交由 sizeof(ProtocolHeader) 自动计算（始终为 11）。
 */
struct Protocol : public ProtocolHeader {
    typedef std::shared_ptr<Protocol> ptr;

    // 固定头部字节数，由字段定义自动推导
    static constexpr uint32_t BASE_LENGTH = sizeof(ProtocolHeader);

    std::string body;

    /**
     * @brief 将协议对象编码为字节数组
     * @param proto 协议对象
     * @return ByteArray::ptr
     */
    static ByteArray::ptr Encode(Protocol::ptr proto);

    /**
     * @brief 从编码后的字节数组解码协议对象
     * @param ba 字节数组对象
     * @return Protocol::ptr
     */
    static Protocol::ptr Decode(ByteArray::ptr ba);
};

} // namespace chen::rpc
