/**
 * @file Protocol.hpp
 * @brief 协议相关定义
 * @author Christins (chen.christins@icloud.com)
 * @date 2026-05-25
 * @copyright GPL-3.0
 */
#pragma once

#include <memory>

#include "../ByteArray/ByteArray.hpp"
#include "../Socket/SocketStream.hpp"

namespace chen {

class Message {
public:
    typedef std::shared_ptr<Message> ptr;
    enum MessageType {
        REQUEST = 1,
        RESPONSE = 2,
        NOTIFY = 3
    };
    virtual ~Message();

    virtual ByteArray::ptr toByteArray();
    virtual bool serializeToByteArray(ByteArray::ptr bytearray) = 0;
    virtual bool parseFromByteArray(ByteArray::ptr bytearray) = 0;

    virtual std::string toString() const = 0;
    virtual const std::string& getName() const = 0;
    virtual int32_t getType() const = 0;
};

class MessageDecoder {
public:
    typedef std::shared_ptr<MessageDecoder> ptr;

    virtual ~MessageDecoder();
    virtual Message::ptr parseFrom(Stream::ptr stream) = 0;
    virtual int32_t serializeTo(Stream::ptr stream, Message::ptr msg) = 0;
};

class Request : public Message {
public:
    typedef std::shared_ptr<Request> ptr;

    Request();

    uint32_t getSn() const { return sn_; }
    uint32_t getCmd() const { return cmd_; }

    void setSn(uint32_t v) { sn_ = v; }
    void setCmd(uint32_t v) { cmd_ = v; }

    virtual bool serializeToByteArray(ByteArray::ptr bytearray) override;
    virtual bool parseFromByteArray(ByteArray::ptr bytearray) override;

protected:
    uint32_t sn_;
    uint32_t cmd_;
};

class Response : public Message {
public:
    typedef std::shared_ptr<Response> ptr;

    Response();

    uint32_t getSn() const { return sn_; }
    uint32_t getCmd() const { return cmd_; }
    uint32_t getResult() const { return result_; }
    const std::string& getResultStr() const { return result_str_; }

    void setSn(uint32_t v) { sn_ = v; }
    void setCmd(uint32_t v) { cmd_ = v; }
    void setResult(uint32_t v) { result_ = v; }
    void setResultStr(const std::string& v) { result_str_ = v; }

    virtual bool serializeToByteArray(ByteArray::ptr bytearray) override;
    virtual bool parseFromByteArray(ByteArray::ptr bytearray) override;

protected:
    uint32_t sn_;
    uint32_t cmd_;
    uint32_t result_;
    std::string result_str_;
};

class Notify : public Message {
public:
    typedef std::shared_ptr<Notify> ptr;
    Notify();

    uint32_t getNotify() const { return notify_; }
    void setNotify(uint32_t v) { notify_ = v; }

    virtual bool serializeToByteArray(ByteArray::ptr bytearray) override;
    virtual bool parseFromByteArray(ByteArray::ptr bytearray) override;

protected:
    uint32_t notify_;
};

} // namespace chen
