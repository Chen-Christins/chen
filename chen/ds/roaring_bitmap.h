/**
 * @file roaring_bitmap.h
 * @brief RoaringBitmap 封装
 */
#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "../bytearray/bytearray.h"
#include "roaring.hpp"

namespace chen {
namespace ds {

class RoaringBitmap {
public:
	typedef std::shared_ptr<RoaringBitmap> ptr;

	RoaringBitmap();
	RoaringBitmap(uint32_t size);
	RoaringBitmap(const RoaringBitmap& bitmap);
	~RoaringBitmap();

	RoaringBitmap& operator=(const RoaringBitmap& oth);

	std::string toString() const;
	bool get(uint32_t idx) const;
	void set(uint32_t idx, bool v);
	
	void set(uint32_t from, uint32_t size, bool v);
	bool get(uint32_t from, uint32_t size, bool v) const;

	RoaringBitmap& operator&=(const RoaringBitmap& oth);
	RoaringBitmap& operator|=(const RoaringBitmap& oth);
	RoaringBitmap& operator-=(const RoaringBitmap& oth);
	RoaringBitmap& operator^=(const RoaringBitmap& oth);

	RoaringBitmap operator&(const RoaringBitmap& oth);
	RoaringBitmap operator|(const RoaringBitmap& oth);
	RoaringBitmap operator-(const RoaringBitmap& oth);
	RoaringBitmap operator^(const RoaringBitmap& oth);

	bool operator==(const RoaringBitmap& oth) const;
	bool operator!=(const RoaringBitmap& oth) const;

	RoaringBitmap::ptr compress() const;
	RoaringBitmap::ptr uncompress() const;

	bool any() const;

	void listPosAsc(std::vector<uint32_t>& pos);
	
	void foreach(std::function<bool(uint32_t)> cb);
	void rforeach(std::function<bool(uint32_t)> cb);

	void writeTo(ByteArray::ptr ba) const;
	bool writeFrom(ByteArray::ptr ba);

	bool cross(const RoaringBitmap& b) const;

	float getCompressRate() const;

	uint32_t getCount() const;
public:
	typedef RoaringSetBitForwardIterator iterator;
	typedef RoaringSetBitReverseIterator reverse_iterator;

	iterator begin() const { return m_bitmap.begin(); }
	iterator end() const { return m_bitmap.end(); }

	reverse_iterator rbegin() const { return m_bitmap.rbegin(); }
	reverse_iterator rend() const { return m_bitmap.rend(); }
private:
	RoaringBitmap(const Roaring& b);
private:
	Roaring m_bitmap;
};

}
}
