SRC  = console-read.c drums3.c echo.c helloworld.c logring.c pager.c\
	drums2.c drums.c ioctl.c uid-filter.c
OBJ  = console-read.o drums3.o echo.o helloworld.o logring.o pager.o\
	drums2.o drums.o ioctl.o uid-filter.o
TARGETS = helloworld

# right now, several examples do not work properly; the current
# version of fusd is unable to add new devices to a preexisting class
# (unless the device calsss is 'sound') and for that reason, examples
# that register multiple devices build but do not run properly.  Will
# be fixed soon (requires a minor kernel extension).

#TARGETS = console-read drums3 echo helloworld logring pager\
#	drums2 drums ioctl uid-filter

default: $(TARGETS)

install: $(TARGETS)

clean:
	rm -f *.o *.d $(TARGETS) gmon.out *~ 

$(TARGETS): %: %.c
	$(CC) $(GCF) $< -o $@ ../libfusd/libfusd.a 

%.d: %.c
	$(CC) -M $(CFLAGS) $< > $@.$$$$; sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; rm -f $@.$$$$

ifeq ($(MAKECMDGOALS),target)
include $(SRC:.c=.d)
endif

