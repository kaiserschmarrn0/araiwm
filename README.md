![araiwm] (https://i.imgur.com/ymgYTgU.png)

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

note that config.h contains an option that compiles in support for an external config file. if compiled in, the binary takes the path to this config file as a cli arg. an example config is provided in the repository.

Launching araiwm
----------------

Display Managers
----------------
edit the dm/startarai script with programs you want to run along araiwm, like a hotkey manager, for example, then install using
	
	make install_dm clean

Todo
----
config branch
