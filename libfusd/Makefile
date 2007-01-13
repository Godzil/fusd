SRC  = libfusd.c
OBJ  = libfusd.o
TARGETS = libfusd.a libfusd.so.0.0

default: $(TARGETS)

install: $(TARGETS)
	$(INSTALL) -d -m 0755 $(LIBDIR)
	$(INSTALL) -m 0755 $(TARGETS) $(LIBDIR)
	/sbin/ldconfig
	$(INSTALL) -d -m 0755 $(INCDIR)
	$(INSTALL) -m 0755 ../include/*.h $(INCDIR)

clean:
	rm -f *.o *.so *.so.* *.a *.d *.d.* gmon.out *~ 

$(TARGETS): 
	$(MAKE) target CFLAGS='-g -O2 $(SCF) $(GCF)'

target: $(OBJ) 
	$(LD) $(OBJ) $(SOLDFLAGS) -o libfusd.so.0.0 $(SLF)
	$(AR) -cr libfusd.a $(OBJ)

%.d: %.c
	$(CC) -M $(CFLAGS) $< > $@.$$$$; sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; rm -f $@.$$$$

ifeq ($(MAKECMDGOALS),target)
include $(SRC:.c=.d)
endif

