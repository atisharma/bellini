Bellini
=========

Bellini is a fork of [CAVA](https://github.com/karlstav/cava/) to provide more a 'scientifically correct' spectrum analyser.
Much of the heavy lifting and infrastructure was done by the author(s) of cava. So credit to them.

Like CAVA, bellini's goal is to provide a audio spectrum analyser for Linux from various inputs.
Unlike CAVA, bellini primary goal is accuracy and correctness, and while CAVA displays to the terminal, bellini writes directly to the framebuffer. This allows some quite different outputs.

Since the aims are somewhat different, and achieving those aims involved changing a substantial amount of the core code, I forked the project.
My hope is that some of the ideas developed here will make their way back upstream over time.

Bellini inherits CAVA's input support, so might work with Pulseaudio, fifo (mpd), sndio, alsa, squeezelite and portaudio. It's been successfully tested on squeezelite and ASLA loopback.

I use it on a Raspberry Pi 4B with the semi-official Buster 64-bit image and a [Pimoroni Hyperpixel 4.0](https://shop.pimoroni.com/products/hyperpixel-4) LCD screen in landscape orientation, and get a smooth 60fps for the FFT vis, using about 50% on one thread, and about 10-15% on the other.
CPU usage could be improved by further optimisation or by sacrificing visual effects.

[![Here is a preview.](https://img.youtube.com/vi/KULyD5bTMlQ/0.jpg)](https://youtu.be/KULyD5bTMlQ "bellini preview")

## Features

Distinguishing features include:

- an accurate two-channel amplitude spectrum on log-log plot (config option `vis=fft`)
- windowing of the data (Hann window by default; Blackman-Nuttall and rectangular also implemented with a code recompilation)
- amplitude spectrum axes marked off at 20dB intervals (amplitude) and powers of 10 / octaves (frequency)
- noise floor truncation (basically the lower axis limit on the amplitude spectrum plot)
- left/right merged colour schemes
- a DIN / Type 1 [Peak Programme Meter](https://en.wikipedia.org/wiki/Peak_programme_meter) (`vis=ppm`)
- a simple plot of the waveform (`vis=pcm`)
- fast direct framebuffer output
- a natty clock
- reloading of the config file if it's been modified


## Installing

Installation and compilation should be almost exactly the same as for CAVA. Please refer to those instructions.
You also need freetype (which you probably already have). On Debian and Void, `ft2build.h` is found in `/usr/include/freetype2/` -- on other distributions you may have to change `Makefile.am` to specify, until I work out how to use automake properly.

The config file should configure the following options:

```
[general]
# noise floor is dB from measured peak amplitude
noise_floor = -100
text_font = /home/pi/bellini/fonts/digital-7/digital-7.ttf
audio_font = /home/pi/bellini/fonts/Gill Sans Pro/GillSansMTPro-Condensed.otf
# decay rate for the fading of the display (< 1.0)
alpha = 0.9
# vis = ppm
# vis = pcm
vis = fft

[input]
# only tested with squeezelite/shmem and ALSA loopback
method = shmem
source = /squeezelite-dc:a6:32:c0:5c:0d

[color]
# hex colors for the following only:
plot_l = "#56FF00"
plot_r = "#007BFF"
ax_2 =   "#FF2100"
ax =     "#468800"
text =   "#FF2100"
audio =  "#888888"
```

The font option must be the full (not relative) path of the font file (look under /usr/share/fonts/).
The fonts I'm using in this example aren't free so you will need to replace them with your choices.

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
Notice the screen rotation. Get that wrong and the code will probably segfault.


Capturing audio
---------------

Audio input should be the same as for CAVA. Please refer to those instructions.


Troubleshooting & FAQ
---------------------


### The flashing cursor is annoying

If the flashing cursor is annoying you, put

    @reboot echo "\e[?25l" > /dev/tty0

in root's crontab. This will kill your cursor (on tty0).


### I'd like to change XYZ option

Some assumptions are hard-coded (for instance the size of the framebuffer, the windowing, and the upper/lower cutoff frequencies of the FFT). Changing these involves at least editing some header file and recompiling, and may break the code. Some of these may be broken out to the config file in the future.


### Bugs

- Input other than squeezelite's shmem and ASLA is untested and may be broken.
- The pause detection currently only works with squeezelite
