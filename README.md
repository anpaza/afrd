Auto Frame Rate Daemon (AFRD)
-----------------------------

This is a Linux daemon for AMLogic SoC based boxes to switch screen
framerate to most suitable during video play. The daemon uses kernel
uevent-based notifications, available in AMLogic 3.14 kernels from
summer 2017. Earlier kernels won't emit notifications at the start
and end of playing video, so the daemon won't work.

Unfortunately, this uevent is gone from kernel 4.9 (used for
Android 7 and 8), so a custom kernel is required for this to work.

The daemon can be linked either with BioniC (for Android) or with
glibc (for plain Linux OSes).

Versions up to 0.1.* use kernel uevent notifications emmited by
AMLogic kernels starting from summer 2017, used in some Android 6.0
and 7.0 firmwares. Unfortunately, somewhere around start of 2018
AMLogic broke its own code and since then these uevents are not
generated (altough part of the respective code is still in kernel).
Here are a example FRAME_RATE_HINT uevent generated when a 29.976
fps movie is started:

change@/devices/virtual/tv/tv
    ACTION=change
    DEVPATH=/devices/virtual/tv/tv
    SUBSYSTEM=tv
    FRAME_RATE_HINT=3203
    MAJOR=254
    MINOR=0
    DEVNAME=tv
    SEQNUM=2787

and when the movie stops:

change@/devices/virtual/tv/tv
    ACTION=change
    DEVPATH=/devices/virtual/tv/tv
    SUBSYSTEM=tv
    FRAME_RATE_END_HINT
    MAJOR=254
    MINOR=0
    DEVNAME=tv
    SEQNUM=2788

Since version 0.2.0 afrd supports detecting movie framerate by
catching uevent's generated when a hardware decoder is started
or stopped. These events are present on both older and newer
kernels, thus afrd can work with almost every AMLogic kernel
out there:

add@/devices/vdec.25/amvdec_h264.0
    ACTION=add
    DEVPATH=/devices/vdec.25/amvdec_h264.0
    SUBSYSTEM=platform
    MODALIAS=platform:amvdec_h264
    SEQNUM=2786

remove@/devices/vdec.25/amvdec_h264.0
    ACTION=remove
    DEVPATH=/devices/vdec.25/amvdec_h264.0
    SUBSYSTEM=platform
    MODALIAS=platform:amvdec_h264
    SEQNUM=2789

These events do not contain the movie frame rate, thus in addition
to these events afrd has to query the video decoder driver, its status
is available from /sys/class/vdec/vdec_status:

vdec channel 0 statistics:
  device name : amvdec_h264
  frame width : 1920
 frame height : 1080
   frame rate : 24 fps
     bit rate : 856 kbps
       status : 63
    frame dur : 4000
   frame data : 19 KB
  frame count : 230
   drop count : 0
fra err count : 0
 hw err count : 0
   total data : 1197 KB

The "frame dur" field is used if not 0 (the actual frame rate is
96000/frame_dur), and "frame rate" field is used otherwise
(with 23 fps being 23.976, 29 being 29.970 and 59 being 59.94 fps).

Configuration file
------------------

AFRD uses a simple configuration file that can be used to customize
its behavior. The file contains either comment lines, starting with
the '#' character, or key=value pairs. The following keywords are
recognized by AFRD:

* hdmi.dev
    This keyword defines the sysfs directory with HDMI driver attributes.
    Usually has the value /sys/class/amhdmitx/amhdmitx0.

* hdmi.state
    This defines the sysfs file used to check if HDMI is enabled or not.
    Usually has the value /sys/class/switch/hdmi/state.

* switch.delay.on
* switch.delay.off
    The time to delay a mode switch when video starts (on) or stops (off).
    The 'on' delay is usually small, while 'off' should be relatively big
    to avoid unneeded mode switches at position change or when you're
    watching a series of videos with same framerate.

    The time is given in milliseconds.

* switch.delay.retry
    Sometimes video decoder doesn't report movie frame rate correctly.
    In these cases afrd will wait some time and try the query again.
    This keyword defines the time interval between these tries.

    The time is given in milliseconds.

* mode.path
    Points to sysfs file used to switch current video mode.
    This is usually /sys/class/display/mode.

* mode.prefer.exact
    If set is 1, AFRD will prefer exact match of video mode refresh rate
    to movie frame rate. If 0 (default setting), AFRD will prefer the
    video mode with highest refresh rate which is a exact multiple of
    movie frame rate.

    Default value is 0.

* mode.use.fract
    Choose how AFRD handles fractional frame rates (23.976, 29.97, 59.94 fps):

    * 0 - Use both fractional and integer refresh rates as appropiate
    * 1 - Prefer fractional refresh rates, even if movie says it uses integer
    * 2 - Prefer integer refresh rates, even if movie says it uses fractional

    This can be used to limit the number of mode switches, e.g. when your OS
    is set up to use 59.94Hz refresh rate (the default on AndroidTV boxes with
    Android 8+) to avoid a mode switch when watching 30 or 60Hz videos
    you can set this to 1.

* mode.blacklist.rates
    This option allows you to blacklist some refresh rates. For example,
    AMLogic kernels have a bug in the HDMI driver that sets the 29.976 mode
    wrong; using this refresh mode causes severe jittering on 23.976 movies.
    Thus, this refresh rate is blacklisted by default.

    This is a list of numbers separated by spaces.

* vdec.status
    This points to sysfs attribute containing the status of the video decoder.
    Usually this is /sys/class/vdec/vdec_status.

* uevent.filter.frhint
* uevent.filter.vdec
* uevent.filter.hdmi
    These sets the filters for kernel uevents we handle:
    * .frhint for the FRAMERATE_HINT uevent generated on some older and patched
        kernels when video decoder detects a change of framerate in played video
    * .vdec for uevents generated by the video decoder when a hardware decoder
        is activated or deactivated
    * .hdmi for uevents generated when we plug in or out the HDMI connector.

    Event filters use the following format:

    uevent.filter.XXX=<keyword>=<regex>[ <keyword>=<regex>...]

    The regular expressions are "extended regular expressions" and are matched
    against the whole attribute value. E.g. regular expression "plat" will not
    match the string "platform", while "platform" or "plat.*" will.

    Regular expressions cannot contain space characters, which are used to
    delimit attribute filters from each other.
