/*
 * AMFData.h
 *
 * Copyright (c) 2007-2008 Dan Weatherford and Facebook, inc.
 * All rights reserved.
 */

#pragma once

#include "common.h"
#include "fout.h"
#include "serialized_buffer.h"
#include <float.h>
#include <math.h>
#include <netinet/in.h>

#ifdef __APPLE__
  #include <architecture/byte_order.h>
  #define hton64(x) OSSwapHostToBigInt64(x)
#else
  #include <byteswap.h>
  #if __BYTE_ORDER == __LITTLE_ENDIAN
    #define hton64(x) bswap_64(x)
  #else
    #define hton64(x) (x)
  #endif
#endif

enum AMFType {
  AMF_TYPE_DOUBLE = 0,
  AMF_TYPE_BOOLEAN = 1,
  AMF_TYPE_STRING = 2,
  AMF_TYPE_OBJECT = 3,
  AMF_TYPE_NULL = 5,
  AMF_TYPE_UNDEFINED = 6,
  AMF_TYPE_MIXED_ARRAY = 8,
  AMF_TYPE_ARRAY = 10,
  AMF_TYPE_DATE = 11,
  AMF_TYPE_UNSUPPORTED = 13,
  AMF_TYPE_UNKNOWN = 255
};

class AMFData {
public:
  virtual ~AMFData() {}
  virtual string asString() const {
    return string("(unknown AMFData type)");
  }

  virtual double asDouble() const {
    return 0.0;
  }

  virtual bool asBool() const {
    return false;
  }

  virtual AMFType typeID() const {
    return AMF_TYPE_UNKNOWN;
  }

  virtual void write(fout& fp) const {
    throw std::runtime_error("AMFData (subtype AMF_TYPE_UNKNOWN): writing this type is meaningless");
  }

  // Factory to produce the right subclass of AMFData object
  static shared_ptr<AMFData> construct(serialized_buffer& buf);

protected:

} ;

class AMFDouble : public AMFData {
public:
  AMFDouble(double _d) : d(_d) {}
  AMFDouble(serialized_buffer& buf) {
    d = buf.get_double_be();
  }
  virtual AMFType typeID() const { return AMF_TYPE_DOUBLE; }
  virtual double asDouble() const { return d; }
  virtual bool asBool() const { return (fabs(d) > FLT_EPSILON); }
  virtual string asString() const {
    char buf[64];
    sprintf(buf, "%f", d);
    return string(buf);
  }
  virtual void write(fout& fp) const {
    union {
      uint64_t a;
      double b;
    } _d;
    _d.b = d;
    fp.putc(AMF_TYPE_DOUBLE);
    fp.write<uint64_t>(hton64(_d.a));
  }

  double d;
} ;

class AMFBoolean : public AMFData {
public:
  AMFBoolean(bool _d) : d(_d) {}
  AMFBoolean(serialized_buffer& buf) {
    d = (buf.get_u8() != 0);
  }

  virtual AMFType typeID() const { return AMF_TYPE_BOOLEAN; }
  virtual double asDouble() const { return d ? 1.0 : 0.0; }
  virtual bool asBool() const { return d; }
  virtual string asString() const {
    return string(d ? "true" : "false");
  }
  virtual void write(fout& fp) const {
    fp.putc(AMF_TYPE_BOOLEAN);
    fp.putc(d ? 0x01 : 0x00);
  }

  bool d;
} ;

class AMFString : public AMFData {
public:
  AMFString(const string& _d) : d(_d) {}
  AMFString() {}
  AMFString(serialized_buffer& buf) {
    uint16_t l = buf.get_u16_be();
    d = string(buf.get_bytes(l), l);
  }

  virtual AMFType typeID() const { return AMF_TYPE_STRING; }
  virtual double asDouble() const { return d.size() ? 1.0 : 0.0; }
  virtual bool asBool() const { return d.size(); }
  virtual string asString() const { return d; }

  virtual void write(fout& fp) const {
    fp.putc(AMF_TYPE_STRING);
    fp.write<uint16_t>(htons(d.size()));
    fp.write(d.data(), d.size());
  }

  string d;
} ;

class AMFNull : public AMFData {
public:
  AMFNull() {}
  AMFNull(serialized_buffer& buf) { }

  virtual AMFType typeID() const { return AMF_TYPE_NULL; }
  virtual double asDouble() const { return 0.0; }
  virtual bool asBool() const { return false; }
  virtual string asString() const { return "NULL"; }
  virtual void write(fout& fp) const {
    fp.putc(AMF_TYPE_NULL);
  }
} ;

class AMFUndefined : public AMFData {
public:
  AMFUndefined() {}
  AMFUndefined(serialized_buffer& buf) { }

  virtual AMFType typeID() const { return AMF_TYPE_UNDEFINED; }
  virtual double asDouble() const { return 0.0; }
  virtual bool asBool() const { return false; }
  virtual string asString() const { return "UNDEFINED"; }
  virtual void write(fout& fp) const {
    fp.putc(AMF_TYPE_UNDEFINED);
  }
} ;

class AMFMixedArray : public AMFData {
public:
  AMFMixedArray() {}
  AMFMixedArray(serialized_buffer& buf) {
    buf.get_u32_be(); //skip the useless nkeys thing that only AMFMixedArray has
                      // (and not AMFObject, which derives from this)
    _construct(buf);
  }

  virtual AMFType typeID() const { return AMF_TYPE_MIXED_ARRAY; }
  virtual double asDouble() const { return dmap.size(); }
  virtual bool asBool() const { return dmap.size(); }
  virtual string asString() const {
    string d("{ \n");
    for (map<string, shared_ptr<AMFData> >::const_iterator dmi = dmap.begin(); dmi != dmap.end(); ++dmi) {
      d = d + string("  ") + dmi->first + string(": ") + dmi->second->asString() + string("\n");
    }
    d += string("}");
    return d;
  }
  // copies the contents of the 'right' array into this one
  // if overwrite is true, right's contents overwrite existing keys in this array
  // if overwrite is false, keys existing in this array will not be copied
  virtual void merge(shared_ptr<AMFData> right, bool overwrite) {
    if (right->typeID() != AMF_TYPE_MIXED_ARRAY) {
      throw std::runtime_error("AMFMixedArray::merge: attempt to merge with something other than a MixedArray");
    }
    AMFMixedArray* r = static_cast<AMFMixedArray*>(&(*right));
    for (map<string, shared_ptr<AMFData> >::const_iterator ri = r->dmap.begin(); ri != r->dmap.end(); ++ri) {
      if (overwrite) {
        dmap[ri->first] = ri->second; // straight assignment operator overwrites existing keys.
      }
      else {
        dmap.insert(*ri); // insert(pair<key, value>) will not overwrite existing keys
      }
    }
  }

  virtual void write(fout& fp) const {
    fp.putc(AMF_TYPE_MIXED_ARRAY);
    fp.write<uint32_t>(htonl(dmap.size())); // mixed arrays have this size thing, but objects don't
    _write(fp);
  }

  map<string, shared_ptr<AMFData> > dmap;
protected:
  void _construct(serialized_buffer& buf) {
    do {
      uint16_t l = 0;
      try {
        l = buf.get_u16_be();
      } catch (const end_of_buffer& e) {
        printf("Error deserializing an array element: %s. Array may be incomplete.\n", e.what());
        return;
      }
      if (l == 0) break; // done
      string k = string(buf.get_bytes(l), l);
      dmap.insert(std::make_pair(k, AMFData::construct(buf)));
    } while (true);
    buf.get_u8(); // eat terminator byte (0x09)
  }
  // writing routines common between this and AMFObject
  void _write(fout& fp) const {
    for (map<string, shared_ptr<AMFData> >::const_iterator dmi = dmap.begin(); dmi != dmap.end(); ++dmi) {
      fp.write<uint16_t>(htons(dmi->first.size()));
      fp.write(dmi->first.data(), dmi->first.size());
      dmi->second->write(fp);
    }
    fp.write<uint16_t>(0);
    fp.putc(0x09); // writeback terminator byte
  }
} ;

class AMFObject : public AMFMixedArray {
public:
  AMFObject() {}
  AMFObject(serialized_buffer& buf) {
    _construct(buf);
  }
  virtual AMFType typeID() const { return AMF_TYPE_OBJECT; }
  virtual void write(fout& fp) const {
    fp.putc(AMF_TYPE_OBJECT);
    _write(fp);
  }
} ;

class AMFArray : public AMFData {
public:
  AMFArray() {}
  AMFArray(serialized_buffer& buf) {
    uint32_t len = buf.get_u32_be();
    for (uint32_t s = 0; s < len; ++s) {
      dmap.push_back(AMFData::construct(buf));
    }
  }

  virtual AMFType typeID() const { return AMF_TYPE_ARRAY; }
  virtual double asDouble() const { return dmap.size(); }
  virtual bool asBool() const { return dmap.size(); }
  virtual string asString() const {
    string d("{ \n");
    for (size_t s = 0; s < dmap.size(); ++s) {
      d = d + string("  ") + dmap[s]->asString() + string("\n");
    }
    d += string("}");
    return d;
  }

  virtual void write(fout& fp) const {
    fp.putc(AMF_TYPE_ARRAY);
    fp.write<uint32_t>(htonl(dmap.size()));
    for (size_t s = 0; s < dmap.size(); ++s) {
      dmap[s]->write(fp);
    }
  }
  vector<shared_ptr<AMFData> > dmap;
} ;

class AMFDate:  public AMFData {
public:
  AMFDate() {
    gettimeofday(&tv, &tz);
  }
  AMFDate(serialized_buffer& buf) {
    // double milliseconds since the epoch
    // int16 TZ offset from UTC
    double s = buf.get_double_be() / 1000.0; // convert from msec
    tv.tv_sec = (time_t) floor(s);
    tv.tv_usec = (time_t) ((s - floor(s)) * 1000000.0);
    tz.tz_minuteswest = buf.get_u16_be();
    tz.tz_dsttime = 0;
  }

  virtual AMFType typeID() const { return AMF_TYPE_DATE; }
  virtual double asDouble() const { return (double)tv.tv_sec + ((double)tv.tv_usec / 1000000.0); }
  virtual bool asBool() const { return (tv.tv_sec != 0); }
  virtual string asString() const {
    char buf[256];
    struct tm tmbuf;
    time_t t = tv.tv_sec - (tz.tz_minuteswest * 60);
    gmtime_r(&t, &tmbuf);
    asctime_r(&tmbuf, buf);
    // drop the newline this thing throws in the buffer
    for (char* c = buf; *c != '\0'; ++c) {
      if (*c == '\n') {
        *c = '\0';
        break;
      }
    }
    return string(buf);
  }

  virtual void write(fout& fp) const {
    fp.putc(AMF_TYPE_DATE);
    // get milliseconds since the epoch
    union {
      uint64_t a;
      double b;
    } _d;
    _d.b = (static_cast<double>(tv.tv_sec) * 1000.0) + (static_cast<double>(tv.tv_usec) / 1000.0);
    fp.write<uint64_t>(hton64(_d.a));
    fp.write<int16_t>(htons(tz.tz_minuteswest));
  }

  struct timeval tv;
  struct timezone tz;
} ;

class AMFUnsupported : public AMFData {
public:
  AMFUnsupported() {}
  AMFUnsupported(serialized_buffer& buf) { }

  virtual AMFType typeID() const { return AMF_TYPE_UNSUPPORTED; }
  virtual double asDouble() const { return 0.0; }
  virtual bool asBool() const { return false; }
  virtual string asString() const { return "UNSUPPORTED"; }
  virtual void write(fout& fp) const {
    fp.putc(AMF_TYPE_UNSUPPORTED);
  }
} ;
