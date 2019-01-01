Auto Frame Rate Daemon (AFRD)
-----------------------------

This is a Linux daemon for AMLogic SoC based boxes to switch screen
framerate to most suitable during video play. The daemon uses kernel
UEVENT-based notifications, available in AMLogic 3.14 kernels from
summer 2017. Earlier kernels won't emit notifications at the start
and end of playing video, so the daemon won't work.

Unfortunately, this UEVENT is gone from kernel 4.9 (used for
Android 7 and 8), so a custom kernel is required for this to work.

The daemon can be linked either with BioniC (for Android) or with
glibc (for plain Linux OSes).
