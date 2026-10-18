#include "serializer.h"

namespace chen::rpc {

void Serializer::SerializeToByteArray(const google::protobuf::Message& msg, ByteArray::ptr ba)
{
    std::string data;
    if (msg.SerializeToString(&data)) {
        ba->writeStringWithoutLength(data);
    }
}

std::string Serializer::SerializeToString(const google::protobuf::Message& msg)
{
    std::string data;
    msg.SerializeToString(&data);
    return data;
}

bool Serializer::DeserializeFromByteArray(google::protobuf::Message& msg, ByteArray::ptr ba, size_t length)
{
    std::string data;
    data.resize(length);
    ba->read(&data[0], length);
    return msg.ParseFromString(data);
}

bool Serializer::DeserializeFromString(google::protobuf::Message& msg, const std::string& data)
{
    return msg.ParseFromString(data);
}

std::string Serializer::DecodeMethod(const std::string& body)
{
    ByteArray::ptr ba(new ByteArray);
    ba->writeStringWithoutLength(body);
    ba->setPosition(0);
    return ba->readStringVint();
}

std::string Serializer::EncodeResponse(int32_t code)
{
    ByteArray::ptr ba(new ByteArray);
    ba->writeFint32(code);
    ba->setPosition(0);
    return ba->toString();
}

bool Serializer::ParseResponse(const std::string& body, int32_t& code, std::string& msg)
{
    ByteArray::ptr ba(new ByteArray);
    ba->writeStringWithoutLength(body);
    ba->setPosition(0);
    code = ba->readFint32();
    if (code != 0) {
        msg = ba->readStringVint();
    }
    return true;
}

void Serializer::serializeArgsToByteArray(ByteArray::ptr ba)
{
}

void Serializer::deserializeArgsFromByteArray(ByteArray::ptr ba)
{
}

void Serializer::write(ByteArray::ptr ba, int8_t v)
{
    ba->writeFint8(v);
}

void Serializer::write(ByteArray::ptr ba, uint8_t v)
{
    ba->writeFuint8(v);
}

void Serializer::write(ByteArray::ptr ba, int16_t v)
{
    ba->writeFint16(v);
}

void Serializer::write(ByteArray::ptr ba, uint16_t v)
{
    ba->writeFuint16(v);
}

void Serializer::write(ByteArray::ptr ba, int32_t v)
{
    ba->writeFint32(v);
}

void Serializer::write(ByteArray::ptr ba, uint32_t v)
{
    ba->writeFuint32(v);
}

void Serializer::write(ByteArray::ptr ba, int64_t v)
{
    ba->writeFint64(v);
}

void Serializer::write(ByteArray::ptr ba, uint64_t v)
{
    ba->writeFuint64(v);
}

void Serializer::write(ByteArray::ptr ba, float v)
{
    ba->writeFloat(v);
}

void Serializer::write(ByteArray::ptr ba, double v)
{
    ba->writeDouble(v);
}

void Serializer::write(ByteArray::ptr ba, bool v)
{
    ba->writeFint8(v ? 1 : 0);
}

void Serializer::write(ByteArray::ptr ba, const std::string& v)
{
    ba->writeStringVint(v);
}

void Serializer::write(ByteArray::ptr ba, const char* v)
{
    ba->writeStringVint(std::string(v));
}

void Serializer::read(ByteArray::ptr ba, int8_t& v)
{
    v = ba->readFint8();
}

void Serializer::read(ByteArray::ptr ba, uint8_t& v)
{
    v = ba->readFuint8();
}

void Serializer::read(ByteArray::ptr ba, int16_t& v)
{
    v = ba->readFint16();
}

void Serializer::read(ByteArray::ptr ba, uint16_t& v)
{
    v = ba->readFuint16();
}

void Serializer::read(ByteArray::ptr ba, int32_t& v)
{
    v = ba->readFint32();
}

void Serializer::read(ByteArray::ptr ba, uint32_t& v)
{
    v = ba->readFuint32();
}

void Serializer::read(ByteArray::ptr ba, int64_t& v)
{
    v = ba->readFint64();
}

void Serializer::read(ByteArray::ptr ba, uint64_t& v)
{
    v = ba->readFuint64();
}

void Serializer::read(ByteArray::ptr ba, float& v)
{
    v = ba->readFloat();
}

void Serializer::read(ByteArray::ptr ba, double& v)
{
    v = ba->readDouble();
}

void Serializer::read(ByteArray::ptr ba, bool& v)
{
    v = ba->readFint8() != 0;
}

void Serializer::read(ByteArray::ptr ba, std::string& v)
{
    v = ba->readStringVint();
}

} // namespace chen::rpc
