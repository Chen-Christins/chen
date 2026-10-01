/**
 * @file handler_map.h
 * @brief RPC 处理器分发表：方法名与 CmdID 共用一张表（key 用 variant 隔离）
 * @author Christins (chen.christins@icloud.com)
 * @date 2026-09-25
 * @copyright GPL-3.0
 */
#pragma once

#include <functional>
#include <string>
#include <variant>

#include "../bytearray/bytearray.h"
#include "protocol.h"

namespace chen::rpc {

/** RPC 处理器签名：接收 (请求体, 请求序列号)，返回响应协议帧 */
using RpcHandler = std::function<Protocol::ptr(ByteArray::ptr, uint32_t)>;

/**
 * @brief 分派键：CmdID（uint32_t）或方法名（std::string）
 * @details 两种分派方式共用一张表，variant 保证互不冲突（方法名 "16"
 *          与 CmdID 16 是不同的 key）。
 */
using HandlerKey = std::variant<uint32_t, std::string>;

/**
 * @brief HandlerKey 的哈希函数（按 alternative 分别计算）
 */
struct HandlerKeyHash {
    size_t operator()(const HandlerKey& key) const;
};

} // namespace chen::rpc
