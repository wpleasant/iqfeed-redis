# GNU/Linux iqfeed-redis
# Written in 2015 by William G. Pleasant <williampleasant@gmail.com> and placed 
# in the PUBLIC DOMAIN. This work is not copyrighted.


LINUXCC=gcc

OPTIMIZATION?=-O3 -mtune=native
WARNINGS=-Wall -W -Wstrict-prototypes -Wwrite-strings
DEBUG?= -g -ggdb
CCFLAGS=$(OPTIMIZATION) $(CFLAGS) $(WARNINGS) $(DEBUG)

redis-iqfeed:
	$(LINUXCC) $(CCFLAGS) -o iqfeed-redis iqfeed-redis.c  -lhiredis

clean:
	rm -f iqfeed-redis

local:
	@make --no-print-directory -C hiredis static
	$(LINUXCC) $(CCFLAGS) -o iqfeed-redis iqfeed-redis.c hiredis/libhiredis.a


cleanlocal:
	rm -f iqfeed-redis
	make -C hiredis clean


install: 
	sudo cp -a iqfeed-redis /usr/local/bin/

