#include "rpc_connection.h"

#include <cstring>

#include "../util/endian.h"

namespace chen::rpc {

RpcConnection::RpcConnection(Socket::ptr sock, bool owner) : SocketStream(sock, owner) {}

namespace {

/**
 * @brief 从字节流解析协议头（固定大端序，与 ByteArray 默认一致）
 * @param buf 定长头部字节流
 * @param[out] hdr 解析成功时填充头部字段
 * @return true 校验通过（magic/version/长度上界），false 非法头部
 */
bool ParseHeader(const char* buf, ProtocolHeader& hdr) {
    size_t pos = 0;
    hdr.magic = ReadBigEndianUint8(buf, pos);
    hdr.version = ReadBigEndianUint8(buf, pos);
    if (hdr.magic != RPC_PROTOCOL_MAGIC || hdr.version != RPC_PROTOCOL_VERSION) {
        return false;
    }
    hdr.type = static_cast<MessageType>(ReadBigEndianUint8(buf, pos));
    hdr.routing = ReadBigEndianUint8(buf, pos);
    hdr.real_random = ReadBigEndianUint8(buf, pos);
    hdr.sequence = ReadBigEndianUint32(buf, pos);
    hdr.length = ReadBigEndianUint32(buf, pos);
    hdr.cmd = ReadBigEndianUint32(buf, pos);
    hdr.src_peer_id = ReadBigEndianUint32(buf, pos);
    hdr.dst_peer_id = ReadBigEndianUint32(buf, pos);
    hdr.func_id = ReadBigEndianUint32(buf, pos);
    hdr.group_id = ReadBigEndianUint32(buf, pos);
    hdr.bind_id = ReadBigEndianUint32(buf, pos);
    hdr.region = ReadBigEndianUint32(buf, pos);

    static constexpr uint32_t MAX_BODY = 16 * 1024 * 1024; // 16MB
    return hdr.length <= MAX_BODY;
}

/**
 * @brief 将协议头编码为字节流（固定大端序）
 */
void EncodeHeader(const ProtocolHeader& hdr, char* buf) {
    size_t pos = 0;
    WriteBigEndianUint8(buf, pos, hdr.magic);
    WriteBigEndianUint8(buf, pos, hdr.version);
    WriteBigEndianUint8(buf, pos, static_cast<uint8_t>(hdr.type));
    WriteBigEndianUint8(buf, pos, hdr.routing);
    WriteBigEndianUint8(buf, pos, hdr.real_random);
    WriteBigEndianUint32(buf, pos, hdr.sequence);
    WriteBigEndianUint32(buf, pos, hdr.length);
    WriteBigEndianUint32(buf, pos, hdr.cmd);
    WriteBigEndianUint32(buf, pos, hdr.src_peer_id);
    WriteBigEndianUint32(buf, pos, hdr.dst_peer_id);
    WriteBigEndianUint32(buf, pos, hdr.func_id);
    WriteBigEndianUint32(buf, pos, hdr.group_id);
    WriteBigEndianUint32(buf, pos, hdr.bind_id);
    WriteBigEndianUint32(buf, pos, hdr.region);
}

} // anonymous namespace

Protocol::ptr RpcConnection::recvProtocol() {
    // 1) 读取定长头部到栈 buffer（避免每次分配 ByteArray）
    char hdr_buf[Protocol::BASE_LENGTH];
    if (readFixSize(hdr_buf, sizeof(hdr_buf)) <= 0) {
        return nullptr;
    }

    // 2) 校验并解析头部
    ProtocolHeader header;
    if (!ParseHeader(hdr_buf, header)) {
        return nullptr;
    }

    // 3) 构造 Protocol，继承的 ProtocolHeader 部分直接赋值
    Protocol::ptr proto(new Protocol);
    static_cast<ProtocolHeader&>(*proto) = header;

    // 4) 读取 body
    if (proto->length > 0) {
        proto->body.resize(proto->length);
        if (readFixSize(&proto->body[0], proto->length) <= 0) {
            return nullptr;
        }
    }

    return proto;
}

int RpcConnection::sendProtocol(Protocol::ptr resp) {
    resp->length = resp->body.size();

    // 头部拼到栈 buffer（避免 ByteArray 分配）
    char hdr_buf[Protocol::BASE_LENGTH];
    EncodeHeader(*resp, hdr_buf);

    // 小包（<=4KB）：header+body 拼到一个栈 buffer，一次 write
    if (resp->body.size() <= 4096) {
        char buf[Protocol::BASE_LENGTH + 4096];
        memcpy(buf, hdr_buf, sizeof(hdr_buf));
        memcpy(buf + sizeof(hdr_buf), resp->body.data(), resp->body.size());
        return writeFixSize(buf, sizeof(hdr_buf) + resp->body.size());
    }

    // 大包：分开写
    int rt = writeFixSize(hdr_buf, sizeof(hdr_buf));
    if (rt <= 0) {
        return rt;
    }
    return writeFixSize(resp->body.data(), resp->body.size());
}

} // namespace chen::rpc