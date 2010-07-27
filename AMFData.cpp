/*
 * AMFData.cpp
 *
 * Copyright (c) 2007-2008 Dan Weatherford and Facebook, inc.
 * All rights reserved.
 */

#include "AMFData.h"

shared_ptr<AMFData> AMFData::construct(serialized_buffer& buf) {
  char typeID = buf.get_u8();
  switch ((typeID & 0xff)) {
    case AMF_TYPE_DOUBLE:
      return shared_ptr<AMFData>(new AMFDouble(buf));
    case AMF_TYPE_BOOLEAN:
      return shared_ptr<AMFData>(new AMFBoolean(buf));
    case AMF_TYPE_STRING:
      return shared_ptr<AMFData>(new AMFString(buf));
    case AMF_TYPE_OBJECT:
      return shared_ptr<AMFData>(new AMFObject(buf));
    /* http://osflash.org/documentation/amf/astypes#x06null */
    case AMF_TYPE_NULL:
      return shared_ptr<AMFData>(new AMFNull(buf));
    /* http://osflash.org/documentation/amf/astypes#x06undefined */
    case AMF_TYPE_UNDEFINED:
      return shared_ptr<AMFData>(new AMFUndefined(buf));
    case AMF_TYPE_MIXED_ARRAY:
      return shared_ptr<AMFData>(new AMFMixedArray(buf));
    case AMF_TYPE_ARRAY:
      return shared_ptr<AMFData>(new AMFArray(buf));
    case AMF_TYPE_DATE:
      return shared_ptr<AMFData>(new AMFDate(buf));
    /* http://osflash.org/documentation/amf/astypes#x06unsupported */
    case AMF_TYPE_UNSUPPORTED:
      return shared_ptr<AMFData>(new AMFUnsupported(buf));
  }
  /// default:
  char errbuf[64];
  sprintf(errbuf, "AMFData::construct: unknown typeID 0x%02x\n", (typeID & 0xff));
  throw std::runtime_error(errbuf);
  return shared_ptr<AMFData>(new AMFData());
}


