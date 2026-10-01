/**
 * @file function_traits.h
 * @brief 函数特征提取模板
 * @author Christins (chen.christins@icloud.com)
 * @date 2026-09-03
 * @copyright GPL-3.0
 */
#pragma once

#include <functional>
#include <tuple>
#include <type_traits>

#include "../bytearray/bytearray.h"
#include "protocol.h"
#include "serializer.h"

namespace chen::rpc {

/**
 * @brief 函数特征提取模板（函数签名萃取）
 *
 * 用于在编译期提取函数/可调用对象的返回类型与参数元组类型，
 * 供 registerMethod 自动做参数反序列化与返回值序列化。
 *
 * 支持：函数指针、std::function、成员函数指针、lambda 及仿函数。
 *
 * 用法：`FunctionTraits<F>::return_type`、`FunctionTraits<F>::args_tuple`。
 */
template <typename T>
struct FunctionTraits : FunctionTraits<decltype(&T::operator())> {};

/**
 * @brief 函数指针特化
 * @tparam R 返回类型
 * @tparam Args 参数类型包
 */
template <typename R, typename... Args>
struct FunctionTraits<R (*)(Args...)> {
    using return_type = R;
    using args_tuple = std::tuple<std::decay_t<Args>...>;
};

/**
 * @brief std::function 特化
 * @tparam R 返回类型
 * @tparam Args 参数类型包
 */
template <typename R, typename... Args>
struct FunctionTraits<std::function<R(Args...)>> {
    using return_type = R;
    using args_tuple = std::tuple<std::decay_t<Args>...>;
};

/**
 * @brief 成员函数指针（非 const）特化
 * @tparam R 返回类型
 * @tparam C 类类型
 * @tparam Args 参数类型包
 */
template <typename R, typename C, typename... Args>
struct FunctionTraits<R (C::*)(Args...)> {
    using return_type = R;
    using args_tuple = std::tuple<std::decay_t<Args>...>;
};

/**
 * @brief 成员函数指针（const）特化
 * @tparam R 返回类型
 * @tparam C 类类型
 * @tparam Args 参数类型包
 */
template <typename R, typename C, typename... Args>
struct FunctionTraits<R (C::*)(Args...) const> {
    using return_type = R;
    using args_tuple = std::tuple<std::decay_t<Args>...>;
};

/**
 * @brief 由可调用对象构造 RPC 处理器
 * @tparam Func 可调用目标类型（函数指针 / std::function / lambda / 仿函数）
 * @param func 实现函数，签名形如 R(Args...)
 * @return 处理器：接收 (ByteArray, Protocol.sequence)，自动反序列化 Args、调用 func、
 *         序列化返回值，返回响应协议帧。异常转为 code=500。
 * @details 服务端（RpcServer）与客户端接收转发请求（RpcClient）共用，
 *          名称分派与 CmdID 分派共用同一处理器。
 */
template <typename Func>
std::function<Protocol::ptr(ByteArray::ptr, uint32_t)> MakeRpcHandler(Func func) {
    return [func](ByteArray::ptr ba, uint32_t sequence) {
        try {
            using Traits = FunctionTraits<Func>;
            typename Traits::args_tuple args;

            Serializer::DeserializeArgsToTuple(ba, args);

            ByteArray::ptr rba(new ByteArray);
            if constexpr (std::is_void_v<typename Traits::return_type>) {
                std::apply(func, args);
                rba->writeFint32(0);  // code = 0
            } else {
                auto result = std::apply(func, args);
                rba->writeFint32(0);  // code = 0
                Serializer::SerializeToByteArray(rba, result);
            }
            rba->setPosition(0);

            Protocol::ptr rsp(new Protocol);
            rsp->type = MessageType::RESPONSE;
            rsp->sequence = sequence;
            rsp->body = rba->toString();
            return rsp;
        } catch (std::exception& e) {
            ByteArray::ptr rba(new ByteArray);
            rba->writeFint32(500);
            rba->writeStringVint(e.what());
            rba->setPosition(0);
            Protocol::ptr rsp(new Protocol);
            rsp->type = MessageType::RESPONSE;
            rsp->sequence = sequence;
            rsp->body = rba->toString();
            return rsp;
        } catch (...) {
            ByteArray::ptr rba(new ByteArray);
            rba->writeFint32(500);
            rba->writeStringVint("Unknown error");
            rba->setPosition(0);
            Protocol::ptr rsp(new Protocol);
            rsp->type = MessageType::RESPONSE;
            rsp->sequence = sequence;
            rsp->body = rba->toString();
            return rsp;
        }
    };
}

} // namespace chen::rpc