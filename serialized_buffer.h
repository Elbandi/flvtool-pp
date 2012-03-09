/*
 * serialized_buffer.h
 *
 * Copyright (c) 2007-2009 Dan Weatherford and Facebook, inc.
 * All rights reserved.
 */

#pragma once
#include <stdexcept>

// start of platform detection
#ifdef __APPLE__ // Mac byteswap defs

#include <architecture/byte_order.h>
#define BE16(x) OSSwapBigToHostInt16(x)
#define BE32(x) OSSwapBigToHostInt32(x)
#define BE64(x) OSSwapBigToHostInt64(x)
#define LE16(x) OSSwapLittleToHostInt16(x)
#define LE32(x) OSSwapLittleToHostInt32(x)
#define LE64(x) OSSwapLittleToHostInt64(x)

#elif defined ( __FreeBSD__ ) // FreeBSD byteswap defs

#include <sys/endian.h>

#define BE16(x) htobe16(x)
#define BE32(x) htobe32(x)
#define BE64(x) htobe64(x)
#define LE16(x) htole16(x)
#define LE32(x) htole32(x)
#define LE64(x) htole64(x)

#else // Linux byteswap defs

#include <byteswap.h>
#include <endian.h>

#if __BYTE_ORDER == __LITTLE_ENDIAN
  #define BE16(x) __bswap_16(x)
  #define BE32(x) __bswap_32(x)
  #define BE64(x) __bswap_64(x)
  #define LE16(x) (x)
  #define LE32(x) (x)
  #define LE64(x) (x)
#else // !__LITTLT_ENDIAN
  #define BE16(x) (x)
  #define BE32(x) (x)
  #define BE64(x) (x)
  #define LE16(x) __bswap_16(x)
  #define LE32(x) __bswap_32(x)
  #define LE64(x) __bswap_64(x)
#endif // __LITTLE_ENDIAN

#endif // end of platform detection

class end_of_buffer : public std::exception {
public:
  end_of_buffer(size_t needed, size_t available) {
    snprintf(msg, 128, "End of buffer reached during a request for %lu bytes (%lu available)", needed, available);
  }
  virtual ~end_of_buffer() throw() {}
  virtual const char* what() const throw() { return msg; }
protected:
  char msg[128];
};

class serialized_buffer {
public:
  serialized_buffer(const char* _p, size_t _len) : p(_p), len(_len) {}

  size_t remaining() const { return len; }
  const char* current() const { return p; }

  const char* get_bytes(size_t bytes) {
    return consume(bytes);
  }

  uint8_t get_u8() {
    return *consume(1);
  }

  uint16_t get_u16_be() {
    return BE16(*reinterpret_cast<const uint16_t*>(consume(2)));
  }
  uint16_t get_u16_le() {
    return LE16(*reinterpret_cast<const uint16_t*>(consume(2)));
  }
  uint32_t get_u24_be() {
    return BE32((*reinterpret_cast<const uint32_t*>(consume(3))) & 0x00ffffff);
  }
  uint32_t get_u24_le() {
    return LE32((*reinterpret_cast<const uint32_t*>(consume(3))) & 0x00ffffff);
  }
  uint32_t get_u32_be() {
    return BE32(*reinterpret_cast<const uint32_t*>(consume(4)));
  }
  uint32_t get_u32_le() {
    return LE32(*reinterpret_cast<const uint32_t*>(consume(4)));
  }
  uint64_t get_u64_be() {
    return BE64(*reinterpret_cast<const uint64_t*>(consume(8)));
  }
  uint64_t get_u64_le() {
    return LE64(*reinterpret_cast<const uint64_t*>(consume(8)));
  }
  double get_double_le() {
    union {
      uint64_t a;
      double b;
    } _d;
    _d.a = get_u64_le();
    return _d.b;
  }
  double get_double_be() {
    union {
      uint64_t a;
      double b;
    } _d;
    _d.a = get_u64_be();
    return _d.b;
  }

protected:
  serialized_buffer(const serialized_buffer& right); // you probably don't want to copy this...

  const char* consume(size_t bytes) {
    if (len < bytes) throw end_of_buffer(bytes, len);

    const char* r = p;
    p += bytes;
    len -= bytes;
    return r;
  }

  const char* p;
  size_t len;
};

