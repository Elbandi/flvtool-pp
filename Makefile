CFLAGS+=-O2 -Wall -I.

# uncomment the following line if you want to install to a different base dir.
#BASEDIR=/mnt/test

PROGRAM = flvtool++
OBJS=flvtool++.o AMFData.o


$(PROGRAM): $(OBJS)
	$(CXX) $(CFLAGS) -o $@ $(OBJS)

install: $(PROGRAM)
	install -d ${BASEDIR}/usr/bin
	install -o root -g root -m 0755 $(PROGRAM) ${BASEDIR}/usr/bin

clean:
	-rm -f $(OBJS) $(PROGRAM)

.SUFFIXES:      .o .cpp
.PHONY: clean install
