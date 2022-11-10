Bellini
=========

Bellini is a fork of [CAVA](https://github.com/karlstav/cava/) initially intended to provide more a 'scientifically correct' spectrum analyser.
It has grown to cover other visualisations also.
The input infrastructure, config file handling is inherited from cava, so credit goes to the author(s) of that project.

With the FFT, bellini primary goal is accuracy and correctness while also looking aesthetically pleasing.

Since the aims are somewhat different from cava, and achieving those aims involved changing a substantial amount of the core code, I forked the project.
My hope is that some of the ideas developed here will make their way back upstream over time.

Bellini inherits CAVA's input support, so might work with Pulseaudio, fifo (mpd), sndio, alsa and squeezelite. It's been successfully tested on squeezelite and ASLA loopback.

I use it on a Raspberry Pi 4B with the semi-official Bullseye 64-bit image and a [Pimoroni Hyperpixel 4.0](https://shop.pimoroni.com/products/hyperpixel-4) LCD screen in landscape orientation, and get a smooth 60fps for the FFT vis, using about 50% on one thread, and about 10-15% on the other.
On my desktop it works very well.
CPU usage could be improved by further optimisation.

Here is a preview video of an older version (before the oscilliscope vis). It's smoother in real life, because of the interaction with the phone's video frame rate.

[![Here is a preview.](https://img.youtube.com/vi/KULyD5bTMlQ/0.jpg)](https://youtu.be/KULyD5bTMlQ "bellini preview")

## Features

### Visualisations
- an audio spectrum analyser (FFT) for Linux (config option `vis=fft`)
- raw PCM (waveform) visualisation (left & right channel vs time) (config option `vis=pcm`)
- An oscilliscope visualisation (left vs right channel) suitable for listening to and viewing [oscilliscope music](https://www.oscilloscopemusic.com) (config option `vis=osc`)
- an old-fashioned DIN / Type 1 [Peak Programme Meter](https://en.wikipedia.org/wiki/Peak_programme_meter) (`vis=ppm`)

### features of the FFT/spectrum visualisation
- an accurate two-channel amplitude spectrum on log-log plot 
- windowing of the data (Hann window by default; Blackman-Nuttall and rectangular also implemented with a code recompilation)
- amplitude spectrum axes marked off at 20dB intervals (amplitude) and powers of 10 / octaves (frequency)
- noise floor truncation (basically the lower axis limit on the amplitude spectrum plot)


### Other features
- fast SDL2 output
- a natty clock
- reloading of the config file if it's been modified
- cute left/right merged colour schemes with phosphor-looking defaults


## Installation and configuration

Installation and compilation should be almost exactly the same as for CAVA. Please refer to those instructions.
You also need freetype (which you probably already have) and `SDL2_ttf`. On Debian and Void, `ft2build.h` is found in `/usr/include/freetype2/` -- on other distributions you may have to change `Makefile.am` to specify, until I work out how to use automake properly.

The config file should configure the following options:

```
[general]
# noise floor is dB from measured peak amplitude
noise_floor = -100
text_font = /home/pi/bellini/fonts/digital-7/digital-7.ttf
audio_font = /home/pi/bellini/fonts/Gill Sans Pro/GillSansMTPro-Condensed.otf
# decay rate for the fading of the display (< 1.0)
alpha = 0.95
# vis = ppm
# vis = pcm
vis = fft

[output]
# it's best to restart after modifying output rotation or size
rotate = 0
width = 960
height = 540
fullscreen = false

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

Output is via SDL. This causes some weirdness using framebuffer output on a Raspberry Pi 4 with a Hyperpixel 4 on raspbian 64bit so I have reverted to running it under wayland in that situation.


Capturing audio
---------------

Audio input should be the same as for CAVA. Please refer to those instructions.


Troubleshooting & FAQ
---------------------


### I'd like to change XYZ option

Most important stuff is in the config file.
Some assumptions are hard-coded (e.g. the upper/lower cutoff frequencies of the FFT). Changing these involves at least editing some header file and recompiling, and may break the code. Some of these may be broken out to the config file in the future.


### Bugs

- Input other than squeezelite's shmem and ASLA is untested and may be broken.
- The pause detection currently only works with squeezelite
- Code quality is a bit hacky in places. There are many things that need cleaning up, dead code etc.
