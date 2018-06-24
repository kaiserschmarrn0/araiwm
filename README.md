araiwm - arai window manager
----------------------------
manages windows no da


Requirements
------------
XCB header files


Configuration
-------------
araiwm is configured by editing config.h and recompiling

Installation
------------
after completing the configuration steps described above, install using

	make install clean

Display Managers
----------------
edit the dm/startarai script with programs you want to run along araiwm, like a hotkey manager, for example, then install using
	
	make install_dm clean

Todo
----
place bar above windows (bar list?)
reparenting?
