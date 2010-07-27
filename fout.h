/*
 * fout.h
 *
 * Copyright (c) 2007-2009 Dan Weatherford and Facebook, inc.
 * All rights reserved.
 */

#pragma once
#define BUFFER_SIZE 32768
#include <string.h> // for strerror
#include <errno.h>
#include <stdint.h>
#include <cstdio>

class fout {
public:
  fout() : fp(NULL), buffer_used(0) {}
  fout(const char* fn) : fp(NULL), buffer_used(0) { this->open(fn); }
  ~fout() { close(); }

  void open(const char* fn) {
    if (fp) this->close();

    fp = fopen(fn, "wb");
    if (fp == NULL) {
      char errbuf[256];
      snprintf(errbuf, 255, "Error opening output file \"%s\": %s", fn, strerror(errno));
      errbuf[255] = '\0';
      throw std::runtime_error(errbuf);
    }
  }

  operator bool() const {
    return (fp != NULL);
  }

  void flush() {
    if (buffer_used) fwrite(buffer, buffer_used, 1, fp);
    buffer_used = 0;
  }

  void close() {
    if (fp) {
      this->flush();
      fclose(fp);
      fp = NULL;
    }
  }

  void write(const char* dat, size_t len) {
    if ((len + buffer_used) > BUFFER_SIZE) {
      this->flush();
    }
    if (len > BUFFER_SIZE) fwrite(dat, len, 1, fp);
    else {
      memcpy(buffer + buffer_used, dat, len);
      buffer_used += len;
    }
  }

  void write_u24_be(uint32_t d) {
    putc((d >> 16) & 0xff);
    putc((d >> 8) & 0xff);
    putc(d & 0xff);
  }

  void putc(char c) {
    if (buffer_used == BUFFER_SIZE) this->flush();
    buffer[buffer_used++] = c;
  }

  uint64_t tell() {
    return (ftello(fp) + ((uint64_t)buffer_used));
  }

  void seek(uint64_t offset, int whence = SEEK_SET) {
    this->flush();
    fseek(fp, offset, whence);
  }

  template <typename T> inline void write(T d) {
    this->write((const char*)&d, sizeof(T));
  }

  template <class Str> inline void write_string(const Str& s) {
    uint32_t sl = s.size();
    this->write((const char*)&sl, sizeof(uint32_t));
    if (sl) this->write(s.data(), sl);
  }

protected:
  FILE* fp;
  uint32_t buffer_used;
  char buffer[BUFFER_SIZE];
private:
  fout(const fout& _r); // noncopyable
  fout& operator=(const fout& _r); // nonassignable
} ;

