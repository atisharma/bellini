Champagne
=========

Champagne is a fork (well, a butchery) of [CAVA](https://github.com/karlstav/cava/) to provide more a 'scientifically correct' spectrum analyzer.
Much of the heavy lifting was done in cava.

Like CAVA, champagne's goal is to provide a audio spectrum analyzer for Linux from various inputs.

Unlike CAVA, champagne is meant for accuracy and correctness, and writes directly (well, will soon...) to the framebuffer for speed.
Although it looks nice with cava's ncurses output.

Champagne inherits CAVA's input support, so should work with Pulseaudio, fifo (mpd), sndio, squeezelite and portaudio.

It also introduces a number of bugs.


Features
--------

Distinguishing features include:

- [x] accurate power spectrum on log-log plot
- [ ] ... with labelled axes
- [ ] accurate noise floor truncation
- [ ] left/right merged colour schemes
- [ ] direct framebuffer output


Installing
----------

Installation should be exactly the same as for CAVA. Please refer to those instructions.


Capturing audio
---------------

Audio input should be exactly the same as for CAVA. Please refer to those instructions.

