/**
 * @file RockProtocol.hpp
 * @brief Rock协议相关定义
 * @author Christins (chen.christins@icloud.com)
 * @date 2026-05-25
 * @copyright GPL-3.0
 */
#pragma once

#include "../ByteArray/ByteArray.hpp"
#include "../Protocol/Protocol.hpp"

namespace chen {

class RockBody {
public:
    typedef std::shared_ptr<RockBody> ptr;
    virtual ~RockBody();

    void setBody(const std::string& v) { body_ = v; }
    const std::string& getBody() const { return body_; }

    virtual bool serializeToByteArray(ByteArray::ptr bytearray);
    virtual bool parseFromByteArray(ByteArray::ptr bytearray);

    template <class T>
    std::shared_ptr<T> getAsPB() const
    {
        try {
            std::shared_ptr<T> data(new T);
            if (data->ParseFromString(body_)) {
                return data;
            }
        } catch (...) {
        }
        return nullptr;
    }

    template <class T>
    bool setAsPB(const T& v)
    {
        try {
            return v.SerializeToString(&body_);
        } catch (...) {
        }
        return false;
    }

protected:
    std::string body_;
};

class RockResponse;
class RockRequest : public Request, public RockBody {
public:
    typedef std::shared_ptr<RockRequest> ptr;

    std::shared_ptr<RockResponse> createResponse();

    virtual std::string toString() const override;
    virtual const std::string& getName() const override;
    virtual int32_t getType() const override;

    virtual bool serializeToByteArray(ByteArray::ptr bytearray) override;
    virtual bool parseFromByteArray(ByteArray::ptr bytearray) override;
};

class RockResponse : public Response, public RockBody {
public:
    typedef std::shared_ptr<RockResponse> ptr;

    virtual std::string toString() const override;
    virtual const std::string& getName() const override;
    virtual int32_t getType() const override;

    virtual bool serializeToByteArray(ByteArray::ptr bytearray) override;
    virtual bool parseFromByteArray(ByteArray::ptr bytearray) override;
};

class RockNotify : public Notify, public RockBody {
public:
    typedef std::shared_ptr<RockNotify> ptr;

    virtual std::string toString() const override;
    virtual const std::string& getName() const override;
    virtual int32_t getType() const override;

    virtual bool serializeToByteArray(ByteArray::ptr bytearray) override;
    virtual bool parseFromByteArray(ByteArray::ptr bytearray) override;
};

struct RockMsgHeader {
    RockMsgHeader();
    uint8_t magic[2];
    uint8_t version;
    uint8_t flag;
    int32_t length;
};

class RockMessageDecoder : public MessageDecoder {
public:
    typedef std::shared_ptr<RockMessageDecoder> ptr;

    virtual Message::ptr parseFrom(Stream::ptr stream) override;
    virtual int32_t serializeTo(Stream::ptr stream, Message::ptr msg) override;
};

} // namespace chen
