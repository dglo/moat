INSTALL_BIN = /usr/local/bin

all:
	make readwrite dtest tcaltest

readwrite: readwrite.c
	gcc -Wall -o readwrite readwrite.c

tcaltest: tcaltest.c 
	gcc -Wall -o tcaltest tcaltest.c

dtest: dtest.c
	gcc -Wall -lcurses -o dtest dtest.c

readgps: readgps.c
	gcc -Wall -o readgps readgps.c

rndpkt: rndpkt.c
	gcc -Wall -o rndpkt rndpkt.c

install: 
	install readwrite  $(INSTALL_BIN)
	install dtest      $(INSTALL_BIN)
	install tcaltest   $(INSTALL_BIN)
	install readgps    $(INSTALL_BIN)
	install rndpkt     $(INSTALL_BIN)
	install watchcomms $(INSTALL_BIN)

clean:
	rm -rf readwrite dtest tcaltest readgps
