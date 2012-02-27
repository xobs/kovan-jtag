all:
	$(CC) jtag.c -o jtag

install:
	cp jtag $(PREFIX)/usr/sbin
