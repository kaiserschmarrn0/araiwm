araiwm - arai window manager
-----------------------
manages windows no da


Requirements
------------
XCB header files


Configuration
-------------
araiwm is configured by editing config.h and recompiling
if you would like to run programs before starting arai (i.e key managers, bars) add them to the startarai script
(in this directory before installation, likely in /usr/local/bin after)


Installation
------------
after completing the configuration steps described above, install using

	make install clean

if you have a display manager, simply select arai at your login screen
otherwise, add `startarai` to your .xinitrc
