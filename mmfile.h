/* 
 * mmfile.h
 *
 * Copyright (c) 2007-2009 Dan Weatherford and Facebook, inc.
 * All rights reserved.
 */

#pragma once

#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdexcept>

class mmfile {
public:
  mmfile() : fd(-1) {} 
  mmfile(char* fn) {
    fd = open(fn, O_RDONLY);
    if (fd == -1) throw std::runtime_error(string("mmfile: unable to open file ") + string(fn));
    struct stat statbuf;
    fstat(fd, &statbuf);
    flen = statbuf.st_size;
    fbase = (char*) mmap(NULL, flen, PROT_READ, MAP_SHARED, fd, 0);
  }

  ~mmfile() {
    this->close();
  }

  void close() {
    if (fd != -1) {
      munmap(fbase, flen);
      ::close(fd); // yep, namespacing. call the stdlib close()
      fd = -1;
    }
  }

  char* fbase;
  size_t flen;
  int fd;
private:
  mmfile(const mmfile& right); // noncopyable
  mmfile& operator=(const mmfile& right); // nonassignable
} ;

