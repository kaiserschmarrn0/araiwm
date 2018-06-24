include config.mk

SRC = araiwm.c
DEPS = config.h types.h
OBJ = $(SRC:.c=.o)

all: araiwm

.c.o:
	$(CC) $(STCFLAGS) -c $<

araiwm.o: config.h

$(OBJ): config.h config.mk

araiwm: $(OBJ)
	$(CC) -o $@ $(OBJ) $(STCFLAGS) $(STLDFLAGS)

clean:
	rm -f araiwm $(OBJ)

install: araiwm
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f araiwm $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/araiwm
	cp -f startarai $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/startarai

install_dm: install
	cp -f arai.desktop /usr/share/xsessions

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/araiwm
