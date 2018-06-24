araiwm - arai window manager
-----------------------
manages windows no da


Requirements
------------
XCB header files


Configuration
-------------
araiwm is configured by editing config.h and recompiling
<br>
if you would like to run programs before starting arai (i.e key managers, bars) add them to the startarai script
(in this directory before installation, likely in /usr/local/bin afterwards)


Installation
------------
after completing the configuration steps described above, install using

	make install clean

<br>
and then add `startarai` to your .xinitrc
<br>
if you are using a display manager, install using


	make install_dm clean

instead

Todo
----
place bar above windows (bar list?)
reparenting?
