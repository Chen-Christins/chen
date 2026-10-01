#include "roaring_bitmap.h"

#include <sstream>

namespace chen {
namespace ds {

RoaringBitmap::RoaringBitmap() {
}

RoaringBitmap::RoaringBitmap(uint32_t size) {
	m_bitmap.addRange(0, size);
}

RoaringBitmap::RoaringBitmap(const RoaringBitmap& b) {
	m_bitmap = b.m_bitmap;
}

RoaringBitmap::RoaringBitmap(const Roaring& bitmap)
	:m_bitmap(bitmap) {
}

RoaringBitmap::~RoaringBitmap() {
}

RoaringBitmap& RoaringBitmap::operator=(const RoaringBitmap& oth) {
	if (this == &oth) {
		return *this;
	}
	m_bitmap = oth.m_bitmap;
	return *this;
}

std::string RoaringBitmap::toString() const {
	std::stringstream ss;
	ss << "[RoaringBitmap count=" << getCount()
	   << " size=" << m_bitmap.getSizeInBytes()
	   << "]";
	return ss.str();
}

void RoaringBitmap::set(uint32_t idx, bool v) {
	if (v) {
		m_bitmap.add(idx);
	} else {
		m_bitmap.remove(idx);
	}
}

void RoaringBitmap::set(uint32_t from, uint32_t size, bool v) {
	m_bitmap.addRange(from, from + size);
}

bool RoaringBitmap::get(uint32_t idx) const {
	return m_bitmap.contains(idx);
}

bool RoaringBitmap::get(uint32_t from, uint32_t size, bool v) const {
	return false;
}

RoaringBitmap& RoaringBitmap::operator&=(const RoaringBitmap& oth) {
	m_bitmap &= oth.m_bitmap;
	return *this;
}

RoaringBitmap& RoaringBitmap::operator|=(const RoaringBitmap& oth) {
	m_bitmap |= oth.m_bitmap;
	return *this;
}

RoaringBitmap& RoaringBitmap::operator-=(const RoaringBitmap& oth) {
	m_bitmap -= oth.m_bitmap;
	return *this;
}

RoaringBitmap& RoaringBitmap::operator^=(const RoaringBitmap& oth) {
	m_bitmap ^= oth.m_bitmap;
	return *this;
}

RoaringBitmap RoaringBitmap::operator&(const RoaringBitmap& oth) {
	return RoaringBitmap(m_bitmap & oth.m_bitmap);
}

RoaringBitmap RoaringBitmap::operator|(const RoaringBitmap& oth) {
	return RoaringBitmap(m_bitmap | oth.m_bitmap);
}

RoaringBitmap RoaringBitmap::operator-(const RoaringBitmap& oth) {
	return RoaringBitmap(m_bitmap - oth.m_bitmap);
}

RoaringBitmap RoaringBitmap::operator^(const RoaringBitmap& oth) {
	return RoaringBitmap(m_bitmap ^ oth.m_bitmap);
}

bool RoaringBitmap::operator==(const RoaringBitmap& oth) const {
	if (this == &oth) {
		return true;
	}
	return m_bitmap == oth.m_bitmap;
}

bool RoaringBitmap::operator!=(const RoaringBitmap& oth) const {
	return !(*this == oth);
}

RoaringBitmap::ptr RoaringBitmap::compress() const {
	RoaringBitmap::ptr rt(new RoaringBitmap(*this));
	rt->m_bitmap.shrinkToFit();
	rt->m_bitmap.runOptimize();
	return rt;
}

RoaringBitmap::ptr RoaringBitmap::uncompress() const {
	RoaringBitmap::ptr rt(new RoaringBitmap(*this));
	rt->m_bitmap.removeRunCompression();
	return rt;
}

bool RoaringBitmap::any() const {
	return m_bitmap.begin() != m_bitmap.end();
}

void RoaringBitmap::listPosAsc(std::vector<uint32_t>& pos) {
	for (auto it = m_bitmap.rbegin(); it != m_bitmap.rend(); ++it) {
		pos.push_back(*it);
	}
}

void RoaringBitmap::foreach(std::function<bool(uint32_t)> cb) {
	for (auto it = m_bitmap.begin(); it != m_bitmap.end(); ++it) {
		if (!cb(*it)) {
			break;
		}
	}
}

void RoaringBitmap::rforeach(std::function<bool(uint32_t)> cb) {
	for (auto it = m_bitmap.rbegin(); it != m_bitmap.rend(); ++it) {
		if (!cb(*it)) {
			break;
		}
	}
}

void RoaringBitmap::writeTo(ByteArray::ptr ba) const {
	size_t size = m_bitmap.getSizeInBytes(false);
	ba->writeFuint32(size);
	std::vector<char> buffer(size);
	m_bitmap.write(buffer.data(), false);
	ba->write(buffer.data(), size);
}

bool RoaringBitmap::writeFrom(ByteArray::ptr ba) {
	try {
		size_t size = ba->readFuint32();
		std::vector<char> buffer(size);
		ba->read(buffer.data(), size);
		m_bitmap = Roaring::read(buffer.data(), false);
		return true;
	} catch(...) {
	}
	return false;
}

bool RoaringBitmap::cross(const RoaringBitmap& b) const {
	return m_bitmap.intersect(b.m_bitmap);
}

float RoaringBitmap::getCompressRate() const {
	return 100;
}

uint32_t RoaringBitmap::getCount() const {
	return m_bitmap.cardinality();
}


}
}
