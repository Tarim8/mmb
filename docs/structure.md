Music Memory Box
================

Contents

- Runtime Start Up      - Details of the start up procedure
- Executables           - Programs that make up the box suite of software
- Directory Summary     - Summary of directories in the repo



Runtime Start Up
----------------

The startup runs the following commands:

### /etc/rc.local
  Called after Raspian is booted  (This may change with a faster boot system).

### box start
  Initialises any required GPIO pins, serial ports, pipes etc

### session
  Creates a deamon "session" which will restart if anything crashes.

### poll DEVICE_LIST | nodejs box.js $HOME/run.d
  The box software.  poll monitors GPIO pins, serial port, etc and pipes any
activity to the box.js software which handles the main functionality.



Executables
-----------

### box/bin/box
  Bash commands to control aspects of the box software.  "box help all" gives a
summary of commands.  "box start" is the only command used in production to
start the box software.

### box/bin/hwctl
  Bash initialisation of GPIO pins.  This is a drop in replacement for the
wiringPi software and is intended to be compatible with any similar /sys/gpio
handling system.

### box/bin/monitor
  Allow remote connections to a Pi through a server - only used in development.

### box/bin/poll
  C++ program to use the system call "poll" to listen to devices, special files
and pipes and write corresponding lines to standard output.  For more
information, "man docs/poll.man".

### box/bin/session
  Bash daemon control.  Runs a process and child processes under a process
group ID that can be started, stopped or automatically restarted.  For more
information, "man docs/session.man".

### box/node_modules/box.js
  Node JS main functionality of the box.  box.js uses the directory structure
under run.d as configuration data.  "ln -s RUN.D_DIR ~/run.d" will create a link to the run.d directory that "box start" will use by default.

### box/boot/boxcfg.txt
  Config file to set the platform, product and hostname.  For Music Memory Box
production units, platform should be "pi"; product should be "mmb" and hostname
should be "meineckbox".  If hostname has changed then the Pi will expand its
file system and reboot (usually indicates first time boot).  This allows a
minimum filesystem image to be copied to a production SD card which will be
expanded on first boot.



Directory Summary
-----------------

- docs/                 - Box documentation
- licences/             - Software licences

### Pi Directories

- box/bin               - Executable
- box/boot              - Modifications to /boot
- box/collection        - Link to sounds data
- box/etc               - Modifications to /etc
- box/node_modules      - Node js module directory
- box/run.d             - Data directory
- box/src/node-...      - Ignore. Instead use: "sudo apt-get install nodejs"
- box/src/poll          - Source for poll binary
- box/var               - Modifications to /var
- box/version           - Version number
- box/wiringPi          - GPIO controls (unused in production)

### Data Directories
These are used by the box.js program.

- box/run.d/collection          - link to sounds directories
- box/run.d/commands            - external commands called by box.js
- box/run.d/platform-dev        - links specific to non-Pi environment
- box/run.d/platform-pi         - links specific to Pi (production) environment
- box/run.d/product-mmb         - links specific to Music Memory Box
- box/run.d/product-trove       - links specific to Trove (unused)
- box/run.d/selection           - commands for different modes
- box/run.d/universal           - commands active in all modes
