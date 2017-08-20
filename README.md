This is a Linux daemon for AMLogic S905/S912-based boxes to switch
screen framerate to most suitable during video play. The daemon uses
kernel UEVENT-based notifications, available in AMLogic 3.14 kernels
from summer 2017. Earlier kernels won't emit notifications at the start
and end of playing video, so the daemon won't work.
