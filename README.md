Champagne
=========

Champagne is a fork (well, more of a butchery) of [CAVA](https://github.com/karlstav/cava/) to provide more a 'scientifically correct' spectrum analyzer.
Much of the heavy lifting and infrastructure was done by the author(s) of cava. So credit to them.

Like CAVA, champagne's goal is to provide a audio spectrum analyzer for Linux from various inputs.
Unlike CAVA, champagne is meant for accuracy and correctness, and writes directly to the framebuffer for speed and precision.

Since the aims are somewhat different, and achieving those involved changing a substantial amount of the core code, I forked the project.

Champagne inherits CAVA's input support, so should work with Pulseaudio, fifo (mpd), sndio, squeezelite and portaudio. It's only tested on squeezelite.

It probably introduces a number of bugs.

I use it on a Raspberry Pi 4B with the semi-official Buster 64-bit image and a Pimoroni Hyperpixel 4.0 LCD screen in landscape orientation, and get a very smooth and responsive 60ps, using about 50% on one thread, and about 15% on the other.


Features
--------

Distinguishing features include:

- an accurate two-channel power spectrum on log-log plot
- windowing of the data using a (3, 3) [Kolmogorov-Zurbenko filter](https://en.wikipedia.org/wiki/Kolmogorov%E2%80%93Zurbenko_filter)
- axes mark off 20dB intervals (power) and powers of 10 / octaves (frequency)
- noise floor truncation
- left/right merged colour schemes
- direct framebuffer output


Installing
----------

Installation and compilation should be almost exactly the same as for CAVA. Please refer to those instructions.
You also need freetype. On Debian, `ft2build.h` is found in `/usr/include/freetype2/` -- you may have to change Makefile.am to specify, until I work out how to use automake properly.

For framebuffer output, the config file should contain only the following options:

```
[general]
# noise floor is dB from measured peak power
noise_floor = -160
framerate = 60

[input]
# only tested with squeezelite/shmem
method = shmem
source = /squeezelite-dc:a6:32:c0:5c:0d

[smoothing]
# decay for the fading of the display
alpha = 0.8
```

To use with a Hyperpixel 4.0, your `/boot/config.txt` should contain:

```
dtoverlay=hyperpixel4
enable_dpi_lcd=1
dpi_group=2
dpi_mode=87
dpi_output_format=0x7f216
dpi_timings=480 0 10 16 59 800 0 15 113 15 0 0 0 60 0 32000000 6

# see https://github.com/pimoroni/hyperpixel4/issues/39 for rotation
display_lcd_rotate=1
#dtoverlay=vc4-fkms-v3d     # <-- enabling this breaks rotation
```

and possibly `arm_64bit=1` if you are using 64 bit.
Notice the screen rotation. Get that wrong and the code will segfault.


Capturing audio
---------------

Audio input should be exactly the same as for CAVA. Please refer to those instructions.
I've only tested the squeezelite input.


Troubleshooting & FAQ
---------------------


## The flashing cursor is annoying

If the flashing cursor is annoying you, put

    @reboot echo "\e[?25l" > /dev/tty0

in root's crontab. This will kill your cursor.


## I'd like to change XYZ option

Some assumptions are hard-coded (for instance the size of the framebuffer, the windowing, the colourscheme, and the upper/lower cutoff frequencies of the FFT). Changing these involves at least editing some header file and recompiling, and may break the code. Some of these may be broken out to the config file in the future.
