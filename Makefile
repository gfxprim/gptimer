CFLAGS=-W -Wall -Wextra -O2 $(shell gfxprim-config --cflags)
LDLIBS=-lm -lrt -lgfxprim $(shell gfxprim-config --libs-widgets)
BIN=gptimer
DEP=$(BIN:=.dep)
CFLAGS+=-DALARM_PATH=\"$(DESTDIR)/usr/share/$(BIN)/alarm.wav\"

all: $(DEP) $(BIN)

%.dep: %.c
	$(CC) $(CFLAGS) -M $< -o $@

-include $(DEP)

install:
	install -m 644 -D layout.json $(DESTDIR)/etc/gp_apps/$(BIN)/layout.json
	install -D $(BIN) -t $(DESTDIR)/usr/bin/
	setcap 'cap_wake_alarm+ep' $(DESTDIR)/usr/bin/$(BIN) || \
		echo "*** FAILED TO SET CAP_WAKE_ALARM ***"
	install -D -m 744 $(BIN).desktop -t $(DESTDIR)/usr/share/applications/
	install -D -m 644 $(BIN).png -t $(DESTDIR)/usr/share/$(BIN)/
	install -D -m 644 alarm.wav -t $(DESTDIR)/usr/share/$(BIN)/

clean:
	rm -f $(BIN) *.dep *.o
