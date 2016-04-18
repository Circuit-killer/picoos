pico]OS
=======

This is a fork of pico]OS realtime operating system.

It was originally created by Dennis Kuschel and Swen Moczarski,
see [original site at sourceforge][1] for details.

Compared to official release it contains updated versions of
ports I have been writing:

- Arm cortex-m0/m3/m4
- Texas Instruments MSP430
- Microchip PIC32
- NPX LPC2xxx (Arm7tdmi)
- Generic Unix (using ucontext(3))

There is some information about these in [my blogs][2].

Other changes are:

- Power management API to provide framework for MCU power saving / sleeping features
- Suppression of timer interrupts during idle mode (tickless idle). Complete implementation is available in cortex-m/stm32 port.
- stdarg support for nano layer printf-functions
- Makefile system uses GNU make pattern rules for source directory handling (otherwise projects that had many directories run into troubles)


Updated doxygen manual is available [here][3].

[1]: http://picoos.sf.net
[2]: http://stonepile.fi/tags/picoos
[3]: http://arizuu.github.io/picoos

