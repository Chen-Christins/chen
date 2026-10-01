/**
 * @file serializer.h
 * @brief RPC序列化与反序列化
 * @author chen
 * @date 2024-06-10
 */
#pragma once

#include <array>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <google/protobuf/message.h>

#include "../bytearray/bytearray.h"

namespace chen::rpc {

/**
 * @brief 自定义类型序列化接口
 * 可以通过特化此模板来支持自定义类型的序列化
 */
template <typename T>
struct Serialization {
    // 如果未特化就调用 write/read，编译时报错，避免静默损坏数据
    static void write(ByteArray::ptr ba, const T& t) {
        static_assert(sizeof(T) != sizeof(T)
            , "Serialization not specialized for this type; "
              "provide template <> struct Serialization<YourType> { ... };");
    }
    static void read(ByteArray::ptr ba, T& t) {
        static_assert(sizeof(T) != sizeof(T)
            , "Serialization not specialized for this type; "
              "provide template <> struct Serialization<YourType> { ... };");
    }
};

/**
 * @class Serializer
 * @brief RPC 参数与消息的序列化/反序列化工具类。
 *
 * - 支持 protobuf Message 与字符串/ByteArray 转换。
 * - 支持将任意参数包按顺序编码为二进制串，或从二进制串按顺序解码。
 * - 字符串采用 Varint64 长度前缀（writeStringVint/readStringVint）。
 */
class Serializer {
public:
    typedef std::shared_ptr<Serializer> ptr;

    template <typename T>
    static void Write(ByteArray::ptr ba, const T& t) {
        write(ba, t);
    }

    template <typename T>
    static void Read(ByteArray::ptr ba, T& t) {
        read(ba, t);
    }

    /**
     * @brief 从protobuf消息对象序列化为字节数组
     * @param msg protobuf消息对象
     * @param ba 字节数组对象
     */
    static void SerializeToByteArray(const google::protobuf::Message& msg, ByteArray::ptr ba) {
        std::string data;
        if (msg.SerializeToString(&data)) {
            ba->writeStringWithoutLength(data);
        }
    }

    /**
     * @brief 从protobuf消息对象序列化为字符串
     * @param msg protobuf消息对象
     * @return std::string
     */
    static std::string SerializeToString(const google::protobuf::Message& msg) {
        std::string data;
        msg.SerializeToString(&data);
        return data;
    }

    /**
     * @brief 从字节数组反序列化
     * @param msg protobuf消息对象
     * @param ba 字节数组对象
     * @param length 数据长度
     * @return bool
     */
    static bool DeserializeFromByteArray(google::protobuf::Message& msg, ByteArray::ptr ba, size_t length) {
        std::string data;
        data.resize(length);
        ba->read(&data[0], length);
        return msg.ParseFromString(data);
    }

    /**
     * @brief 从字符串反序列化
     * @param msg protobuf消息对象
     * @param data 字符串数据
     * @return bool
     */
    static bool DeserializeFromString(google::protobuf::Message& msg, const std::string& data) {
        return msg.ParseFromString(data);
    }

    /**
     * @brief 序列化任意类型参数到 string（用于赋值给 RpcRequest.args）
     * @tparam Args 参数类型包（按传入顺序依次编码）
     * @param args 需要序列化的参数列表
     * @return 编码后的二进制串
     */
    template <typename... Args>
    static std::string SerializeArgs(Args... args) {
        ByteArray::ptr ba(new ByteArray);
        serializeArgsToByteArray(ba, args...);
        ba->setPosition(0);
        return ba->toString();
    }

    /**
     * @brief 从 string 反序列化参数（用于从 RpcRequest.args 解析）
     * @tparam Args 参数类型包（按写入顺序依次读取）
     * @param data 二进制串
     * @param args 输出参数引用（依次被赋值）
     */
    template <typename... Args>
    static void DeserializeArgs(const std::string& data, Args&... args) {
        ByteArray::ptr ba(new ByteArray(data.size()));
        ba->writeStringWithoutLength(data);
        ba->setPosition(0);
        deserializeArgsFromByteArray(ba, args...);
    }

    /**
     * @brief 从 string 反序列化参数到元组（用于从 RpcRequest.args 解析）
     * @tparam Tuple 目标元组类型，如 std::tuple<int, std::string>
     * @param data 二进制串
     * @param t 输出元组（各元素依序被赋值）
     */
    template <typename Tuple>
    static void DeserializeArgsToTuple(const std::string& data, Tuple& t) {
        ByteArray::ptr ba(new ByteArray(data.size()));
        ba->writeStringWithoutLength(data);
        ba->setPosition(0);

        std::apply([ba](auto&&... args) {
            deserializeArgsFromByteArray(ba, args...);
        }, t);
    }

    // ─── 以下为新版直接 Body 编码接口，无需 protobuf ───

    /**
     * @brief 将方法名 + 参数直接编码为 body（替代 protobuf RpcRequest）
     * @tparam Args 参数类型包
     * @param method 方法名
     * @param args 参数值
     * @return body 二进制串（[method_vint][args...]）
     */
    template <typename... Args>
    static std::string EncodeRequest(const std::string& method, const Args&... args) {
        ByteArray::ptr ba(new ByteArray);
        ba->writeStringVint(method);
        serializeArgsToByteArray(ba, args...);
        ba->setPosition(0);
        return ba->toString();
    }

    /**
     * @brief 从 request body 中提取方法名
     * @param body request body
     * @return std::string 方法名
     */
    static std::string DecodeMethod(const std::string& body) {
        ByteArray::ptr ba(new ByteArray);
        ba->writeStringWithoutLength(body);
        ba->setPosition(0);
        return ba->readStringVint();
    }

    /**
     * @brief 从 ByteArray（position 已在 method 之后）反序列化 args 到元组
     * @tparam Tuple 目标元组类型
     * @param ba ByteArray，需已跳过 method
     * @param t 输出元组
     */
    template <typename Tuple>
    static void DeserializeArgsToTuple(ByteArray::ptr ba, Tuple& t) {
        std::apply([ba](auto&&... args) {
            deserializeArgsFromByteArray(ba, args...);
        }, t);
    }

    /**
     * @brief 序列化可变参数到 ByteArray（公开包装）
     * @tparam Args 参数类型包
     * @param ba 目标 ByteArray
     * @param args 参数值
     */
    template <typename... Args>
    static void SerializeToByteArray(ByteArray::ptr ba, const Args&... args) {
        serializeArgsToByteArray(ba, args...);
    }

    /**
     * @brief 将响应编码为 body（替代 protobuf RpcResponse）
     * @tparam T 返回值类型（void 时传 int 占位即可）
     * @param code 错误码（0=成功）
     * @param msg 错误信息（code!=0 时使用）
     * @param result 返回值（code==0 时使用，void 时不传此参数）
     * @return body 二进制串（[code_int32][result_binary] 或 [code_int32][message_vint]）
     */
    template <typename T>
    static std::string EncodeResponse(int32_t code, const std::string& msg, const T& result) {
        ByteArray::ptr ba(new ByteArray);
        ba->writeFint32(code);
        if (code != 0) {
            ba->writeStringVint(msg);
        } else {
            serializeArgsToByteArray(ba, result);
        }
        ba->setPosition(0);
        return ba->toString();
    }

    /// void 返回值专用（仅写入 code）
    static std::string EncodeResponse(int32_t code = 0) {
        ByteArray::ptr ba(new ByteArray);
        ba->writeFint32(code);
        ba->setPosition(0);
        return ba->toString();
    }

    /**
     * @brief 解析响应头部（code + message）
     * @param body response body
     * @param[out] code 错误码
     * @param[out] msg 错误信息
     * @return true 解析成功
     */
    static bool ParseResponse(const std::string& body, int32_t& code, std::string& msg) {
        ByteArray::ptr ba(new ByteArray);
        ba->writeStringWithoutLength(body);
        ba->setPosition(0);
        code = ba->readFint32();
        if (code != 0) {
            msg = ba->readStringVint();
        }
        return true;
    }

    /**
     * @brief 从 response body 反序列化返回值（跳过前 4 字节 code）
     * @tparam T 返回值类型
     * @param body response body
     * @param[out] result 返回值
     */
    template <typename T>
    static void DeserializeResult(const std::string& body, T& result) {
        ByteArray::ptr ba(new ByteArray);
        ba->writeStringWithoutLength(body);
        ba->setPosition(0);
        ba->readFint32();  // skip code
        deserializeArgsFromByteArray(ba, result);
    }

private:
    // 递归序列化辅助函数（终止条件，无操作）
    static void serializeArgsToByteArray(ByteArray::ptr ba) {}

    /**
     * @brief 递归序列化辅助函数：写入首参并递归处理余参
     * @tparam T 首个参数类型
     * @tparam Args 剩余参数类型包
     * @param ba 目标字节数组
     * @param t 首个参数值
     * @param args 剩余参数值
     */
    template <typename T, typename... Args>
    static void serializeArgsToByteArray(ByteArray::ptr ba, const T& t, Args... args) {
        write(ba, t);
        serializeArgsToByteArray(ba, args...);
    }

    // 递归反序列化辅助函数（终止条件，无操作）
    static void deserializeArgsFromByteArray(ByteArray::ptr ba) {}

    /**
     * @brief 递归反序列化辅助函数：读取首参并递归处理余参
     * @tparam T 首个参数类型
     * @tparam Args 剩余参数类型包
     * @param ba 源字节数组
     * @param t 首个参数引用
     * @param args 剩余参数引用
     */
    template <typename T, typename... Args>
    static void deserializeArgsFromByteArray(ByteArray::ptr ba, T& t, Args&... args) {
        read(ba, t);
        deserializeArgsFromByteArray(ba, args...);
    }

    // 针对不同类型的 write 重载
    static void write(ByteArray::ptr ba, int8_t v) { ba->writeFint8(v); }
    static void write(ByteArray::ptr ba, uint8_t v) { ba->writeFuint8(v); }
    static void write(ByteArray::ptr ba, int16_t v) { ba->writeFint16(v); }
    static void write(ByteArray::ptr ba, uint16_t v) { ba->writeFuint16(v); }
    static void write(ByteArray::ptr ba, int32_t v) { ba->writeFint32(v); }
    static void write(ByteArray::ptr ba, uint32_t v) { ba->writeFuint32(v); }
    static void write(ByteArray::ptr ba, int64_t v) { ba->writeFint64(v); }
    static void write(ByteArray::ptr ba, uint64_t v) { ba->writeFuint64(v); }
    static void write(ByteArray::ptr ba, float v) { ba->writeFloat(v); }
    static void write(ByteArray::ptr ba, double v) { ba->writeDouble(v); }
    static void write(ByteArray::ptr ba, bool v) { ba->writeFint8(v ? 1 : 0); }
    static void write(ByteArray::ptr ba, const std::string& v) { ba->writeStringVint(v); }
    static void write(ByteArray::ptr ba, const char* v) { ba->writeStringVint(std::string(v)); }

    // 针对 std::vector 的序列化
    template <typename T>
    static void write(ByteArray::ptr ba, const std::vector<T>& v) {
        ba->writeUint32(v.size());
        for (const auto& item : v) {
            write(ba, item);
        }
    }

    // 针对 std::list 的序列化
    template <typename T>
    static void write(ByteArray::ptr ba, const std::list<T>& v) {
        ba->writeUint32(v.size());
        for (const auto& item : v) {
            write(ba, item);
        }
    }

    // 针对 std::set 的序列化
    template <typename T>
    static void write(ByteArray::ptr ba, const std::set<T>& v) {
        ba->writeUint32(v.size());
        for (const auto& item : v) {
            write(ba, item);
        }
    }

    // 针对 std::unordered_set 的序列化
    template <typename T>
    static void write(ByteArray::ptr ba, const std::unordered_set<T>& v) {
        ba->writeUint32(v.size());
        for (const auto& item : v) {
            write(ba, item);
        }
    }

    // 针对 std::map 的序列化
    template <typename K, typename V>
    static void write(ByteArray::ptr ba, const std::map<K, V>& m) {
        ba->writeUint32(m.size());
        for (const auto& [key, value] : m) {
            write(ba, key);
            write(ba, value);
        }
    }

    // 针对 std::unordered_map 的序列化
    template <typename K, typename V>
    static void write(ByteArray::ptr ba, const std::unordered_map<K, V>& m) {
        ba->writeUint32(m.size());
        for (const auto& [key, value] : m) {
            write(ba, key);
            write(ba, value);
        }
    }

    // 针对 std::array 的序列化
    template <typename T, size_t N>
    static void write(ByteArray::ptr ba, const std::array<T, N>& v) {
        ba->writeUint32(static_cast<uint32_t>(v.size()));
        for (const auto& item : v) {
            write(ba, item);
        }
    }

    // 针对不同类型的 read 重载
    static void read(ByteArray::ptr ba, int8_t& v) { v = ba->readFint8(); }
    static void read(ByteArray::ptr ba, uint8_t& v) { v = ba->readFuint8(); }
    static void read(ByteArray::ptr ba, int16_t& v) { v = ba->readFint16(); }
    static void read(ByteArray::ptr ba, uint16_t& v) { v = ba->readFuint16(); }
    static void read(ByteArray::ptr ba, int32_t& v) { v = ba->readFint32(); }
    static void read(ByteArray::ptr ba, uint32_t& v) { v = ba->readFuint32(); }
    static void read(ByteArray::ptr ba, int64_t& v) { v = ba->readFint64(); }
    static void read(ByteArray::ptr ba, uint64_t& v) { v = ba->readFuint64(); }
    static void read(ByteArray::ptr ba, float& v) { v = ba->readFloat(); }
    static void read(ByteArray::ptr ba, double& v) { v = ba->readDouble(); }
    static void read(ByteArray::ptr ba, bool& v) { v = ba->readFint8() != 0; }
    static void read(ByteArray::ptr ba, std::string& v) { v = ba->readStringVint(); }

    // 针对 std::vector 的反序列化
    template <typename T>
    static void read(ByteArray::ptr ba, std::vector<T>& v) {
        uint32_t size = ba->readUint32();
        v.clear();
        v.reserve(size);
        for (uint32_t i = 0; i < size; ++i) {
            T item;
            read(ba, item);
            v.push_back(std::move(item));
        }
    }

    // 针对 std::list 的反序列化
    template <typename T>
    static void read(ByteArray::ptr ba, std::list<T>& v) {
        uint32_t size = ba->readUint32();
        v.clear();
        for (uint32_t i = 0; i < size; ++i) {
            T item;
            read(ba, item);
            v.push_back(std::move(item));
        }
    }

    // 针对 std::set 的反序列化
    template <typename T>
    static void read(ByteArray::ptr ba, std::set<T>& v) {
        uint32_t size = ba->readUint32();
        v.clear();
        for (uint32_t i = 0; i < size; ++i) {
            T item;
            read(ba, item);
            v.insert(std::move(item));
        }
    }

    // 针对 std::unordered_set 的反序列化
    template <typename T>
    static void read(ByteArray::ptr ba, std::unordered_set<T>& v) {
        uint32_t size = ba->readUint32();
        v.clear();
        v.reserve(size);
        for (uint32_t i = 0; i < size; ++i) {
            T item;
            read(ba, item);
            v.insert(std::move(item));
        }
    }

    // 针对 std::map 的反序列化
    template <typename K, typename V>
    static void read(ByteArray::ptr ba, std::map<K, V>& m) {
        uint32_t size = ba->readUint32();
        m.clear();
        for (uint32_t i = 0; i < size; ++i) {
            K key;
            V value;
            read(ba, key);
            read(ba, value);
            m[std::move(key)] = std::move(value);
        }
    }

    // 针对 std::unordered_map 的反序列化
    template <typename K, typename V>
    static void read(ByteArray::ptr ba, std::unordered_map<K, V>& m) {
        uint32_t size = ba->readUint32();
        m.clear();
        m.reserve(size);
        for (uint32_t i = 0; i < size; ++i) {
            K key;
            V value;
            read(ba, key);
            read(ba, value);
            m[std::move(key)] = std::move(value);
        }
    }

    // 针对 std::array 的反序列化
    template <typename T, size_t N>
    static void read(ByteArray::ptr ba, std::array<T, N>& v) {
        uint32_t count = ba->readUint32();
        uint32_t fill = count < static_cast<uint32_t>(N) ? count : static_cast<uint32_t>(N);
        for (uint32_t i = 0; i < fill; ++i) {
            read(ba, v[i]);
        }
        for (uint32_t i = fill; i < count; ++i) {
            T item{};
            read(ba, item);
        }
    }

    // 通用序列化接口，调用 Serialization<T>
    template <typename T>
    static void write(ByteArray::ptr ba, const T& t) {
        Serialization<T>::write(ba, t);
    }

    // 通用反序列化接口，调用 Serialization<T>
    template <typename T>
    static void read(ByteArray::ptr ba, T& t) {
        Serialization<T>::read(ba, t);
    }
};

} // namespace chen::rpc
