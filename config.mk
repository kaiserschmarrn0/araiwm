VERSION = 0.0.0

PREFIX = /usr/local
MANPREFIX = $(PREFIX)/share/man

X11INC = /usr/X11R6/include
X11LIB = /usr/X11R6/lib

INCS = -I$(X11INC)
LIBS = -L$(X11LIB) -lxcb -lxcb-keysyms -lxcb-ewmh -lxcb-icccm

STCFLAGS = $(INCS)
STLDFLAGS = $(LIBS)
