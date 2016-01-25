Using a USB Stick with Music Memory Box
=======================================

You can use a USB Stick with Music Memory Box to:
* Add new recordings
* Backup recordings on a Music Memory Box
* Restore or clear recordings from a Music Memory Box

Adding new recordings
---------------------

Create a folder and sub-folder called:

/musicmemorybox/add/

And into it, put any .mp3 or .wav files you want added to the box.  When the
USB stick is inserted; these files will be copied over to the Music Memory Box.
Note that, these sounds still won't have tags associated with them - these will
have to be added using the rotary knob and the (+) add card.


Backup recordings
-----------------

If the USB stick has a folder called /musicmemorybox/ at the top level then
Music Memory Box will create a backup folder.  The backup will be called
something like:

/musicmemorybox/backup-boxname-number/

and will uniquely identify the backup.


Restore recordings
------------------

To restore recordings, rename the backup folder that was created by the above
process as:

/musicmemorybox/restore

and insert the USB Stick.  The files on the Music Box will be replaced with the
contents of the restore folder.

Alternatively, using an SD card from another Music Memory Box in a USB stick
converter will replace the contents with the files from the USB stick.  Note
that SD cards are often treated as two separate file systems and may give a
message about the USB stick being empty before copying from the file system
that has the desired files on it.

