Trove/Music Memory Box Platform Requirements
============================================

Trove and Music Memory box use the same software - which will run on an
embedded Linux system with the following features:

* GPIO pins accessible through /sys/class/gpio drivers  

* Audio input (Trove) and output devices  
    This can be through a USB audio card.  

* USB Port (for memory stick)  
     Separate from any USB audio port.  

* Node JS  
    Current version 0.10.22.  
    Soon to be upgraded to Node JS v4 LTS. 

* Sox  
    Currently version 0.14 but most should work.  

The system runs on a Raspberry Pi and is expected that it would port to boards
like the BeagleBone Black and C.H.I.P. without problem.
