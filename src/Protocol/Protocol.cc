#include "Protocol.hpp"

namespace chen {

Message::~Message()
{
}

MessageDecoder::~MessageDecoder()
{
}

ByteArray::ptr Message::toByteArray()
{
    ByteArray::ptr ba(new ByteArray);
    if (serializeToByteArray(ba)) {
        return ba;
    }
    return nullptr;
}

Request::Request()
    : sn_(0)
    , cmd_(0)
{
}

bool Request::serializeToByteArray(ByteArray::ptr bytearray)
{
    bytearray->writeFuint8(getType());
    bytearray->writeUint32(sn_);
    bytearray->writeUint32(cmd_);
    return true;
}

bool Request::parseFromByteArray(ByteArray::ptr bytearray)
{
    sn_ = bytearray->readUint32();
    cmd_ = bytearray->readUint32();
    return true;
}

Response::Response()
    : sn_(0)
    , cmd_(0)
    , result_(404)
    , result_str_("unhandle")
{
}

bool Response::serializeToByteArray(ByteArray::ptr bytearray)
{
    bytearray->writeFuint8(getType());
    bytearray->writeUint32(sn_);
    bytearray->writeUint32(cmd_);
    bytearray->writeUint32(result_);
    bytearray->writeStringVint(result_str_);
    return true;
}

bool Response::parseFromByteArray(ByteArray::ptr bytearray)
{
    sn_ = bytearray->readUint32();
    cmd_ = bytearray->readUint32();
    result_ = bytearray->readUint32();
    result_str_ = bytearray->readStringVint();
    return true;
}

Notify::Notify()
    : notify_(0)
{
}

bool Notify::serializeToByteArray(ByteArray::ptr bytearray)
{
    bytearray->writeFuint8(getType());
    bytearray->writeUint32(notify_);
    return true;
}

bool Notify::parseFromByteArray(ByteArray::ptr bytearray)
{
    notify_ = bytearray->readUint32();
    return true;
}

} // namespace chen
