![araiwm](https://raw.github.com/kaiserschmarrn0/araiwm/master/araiwm.png)

araiwm - arai window manager
----------------------------
araiwm is a truly minimal, genuinely useful window manager for X no da

be careful, we ignore many standards (but only because they're awful standards)

Requirements
------------
XCB header files

	Debian, Ubuntu: libxcb1-dev libxcb-ewmh-dev libxcb-icccm4-dev libxcb-keysyms1-dev
	Void: libxcb-devel xcb-util-devel xcb-util-keysyms-devel xcb-util-wm-devel

Configuration
-------------
for now, araiwm is configured by editing araiwm.c.

Installation
------------
after completing the configuration steps described above, install using

	make install clean
