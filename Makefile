SRC = araiwm.c
OBJ = $(SRC:.c=.o)

PREFIX = /usr/local

all: araiwm

.c.o:
	$(CC) -I/usr/X11R6/include -c  $<

araiwm: $(OBJ)
	$(CC) -o $@ $(OBJ) -I/usr/X11R6/include -L/usr/X11R6/lib -lxcb -lxcb-keysyms -lxcb-ewmh -lxcb-icccm

install:
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f araiwm $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/araiwm

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/araiwm $(OBJ)

clean:
	rm -f araiwm $(OBJ)
