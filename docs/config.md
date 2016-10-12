Configuration
=============

When the image has been copied to an SD card it may be configured by altering
the file _/boot/boxcfg.txt_.

The _/boot_ filesystem is FAT so should be readable by most computers.  The
_boxcfg.txt_ file may be edited with most standard text editors.


Keywords
--------

Any blank line or line starting with # is treated as a comment.

* platform
    Can be _pi_ or _dev_.  Only use _dev_ if you know what you're doing.

* product
    Can be _trove_ or _mmb_.  At some point, other languages may be added here.

* hostname
    Set the name of the host for remote access.


Values on a fresh image
-----------------------

    platform pi
    product trove
    hostname meineckbox
