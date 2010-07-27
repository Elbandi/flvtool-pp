/*
 * bitstream.h
 *
 * Copyright (c) 2007-2009 Dan Weatherford and Facebook, inc.
 * All rights reserved.
 */

#pragma once
#include "serialized_buffer.h"
#include <assert.h>

class bitstream {
public:
  bitstream(serialized_buffer* _buffer) : buffer(_buffer), current_byte(0), last_used_bit(0) {}

  // I'm sure this is really slow, but this isn't really in a critical path...
  uint32_t get_bits(uint8_t nbits) {
    uint32_t result = 0;
    assert(nbits <= 32);
    while (nbits) {
      result = result << 1 | get_bit();
      --nbits;
    }
    return result;
  }

  uint8_t get_bit() {
    if (! last_used_bit) refill();
    --last_used_bit;
    return ((current_byte & (1 << last_used_bit)) >> last_used_bit) & 1;
  }

  uint32_t get_golomb_ue() {
    uint32_t leading_zeros = 0;
    while (! get_bit()) ++leading_zeros;
    return ((1 << leading_zeros) | get_bits(leading_zeros)) - 1;
  }

  int32_t get_golomb_se() {
    uint32_t ue = get_golomb_ue();
    if (! ue) return 0;
    else if (ue & 1) return (ue >> 1);
    else return (0 - (ue >> 1));
  }

protected:
  void refill() {
    assert(last_used_bit == 0);
    current_byte = buffer->get_u8();
    last_used_bit = 8;
  }

  serialized_buffer* buffer;
  uint8_t current_byte;
  uint8_t last_used_bit;

private:
  bitstream& operator=(const bitstream&);
  bitstream(const bitstream&);
};

