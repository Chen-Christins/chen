#include "protocol.h"

namespace chen::rpc {

ByteArray::ptr Protocol::Encode(Protocol::ptr proto) {
    ByteArray::ptr ba(new ByteArray);
    ba->writeFuint8(proto->magic);
    ba->writeFuint8(proto->version);
    ba->writeFuint8(static_cast<uint8_t>(proto->type));
    ba->writeFuint8(proto->routing);
    ba->writeFuint8(proto->real_random);
    ba->writeFuint32(proto->sequence);

    proto->length = proto->body.size();
    ba->writeFuint32(proto->length);

    ba->writeFuint32(proto->cmd);
    ba->writeFuint32(proto->src_peer_id);
    ba->writeFuint32(proto->dst_peer_id);
    ba->writeFuint32(proto->func_id);
    ba->writeFuint32(proto->group_id);
    ba->writeFuint32(proto->bind_id);
    ba->writeFuint32(proto->region);

    ba->writeStringWithoutLength(proto->body);
    ba->setPosition(0);
    return ba;
}

Protocol::ptr Protocol::Decode(ByteArray::ptr ba) {
    Protocol::ptr proto(new Protocol);
    proto->magic = ba->readFuint8();
    proto->version = ba->readFuint8();
    proto->type = static_cast<MessageType>(ba->readFuint8());
    proto->routing = ba->readFuint8();
    proto->real_random = ba->readFuint8();
    proto->sequence = ba->readFuint32();
    proto->length = ba->readFuint32();

    proto->cmd = ba->readFuint32();
    proto->src_peer_id = ba->readFuint32();
    proto->dst_peer_id = ba->readFuint32();
    proto->func_id = ba->readFuint32();
    proto->group_id = ba->readFuint32();
    proto->bind_id = ba->readFuint32();
    proto->region = ba->readFuint32();

    proto->body.resize(proto->length);
    if (proto->length > 0) {
        ba->read(&proto->body[0], proto->length);
    }
    return proto;
}

} // namespace chen::rpc
