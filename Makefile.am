AUTOMAKE_OPTIONS = foreign

if !SYSTEM_LIBINIPARSER
    SUBDIRS = iniparser
endif

ACLOCAL_AMFLAGS = -I m4

M_CPPFLAGS = -DSYSTEM_LIBINIPARSER=@SYSTEM_LIBINIPARSER@

bin_PROGRAMS = champagne
champagne_SOURCES = champagne.c config.c input/common.c input/fifo.c input/shmem.c \
					sigproc.c \
					output/framebuffer.c output/fbplot.c
champagne_LDFLAGS = -L/usr/local/lib -Wl,-rpath /usr/local/lib
champagne_CPPFLAGS = -DPACKAGE=\"$(PACKAGE)\" -DVERSION=\"$(VERSION)\" \
           -D_POSIX_SOURCE -D _POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE_EXTENDED
champagne_CFLAGS = -std=c99 -Wall -Werror -Wextra -Wno-unused-result -Wno-unknown-warning-option -Wno-maybe-uninitialized

if OSX
    champagne_CFLAGS += -DNORT
else
    champagne_LDFLAGS += -lrt
endif

if ALSA
    champagne_SOURCES += input/alsa.c
endif

if PORTAUDIO
    champagne_SOURCES += input/portaudio.c
endif

if PULSE
    champagne_SOURCES += input/pulse.c
endif

if SNDIO
    champagne_SOURCES += input/sndio.c
endif

if !SYSTEM_LIBINIPARSER
    champagne_LDADD = -liniparser
    champagne_SOURCES += iniparser/libiniparser.la
    champagne_LDADD += -Liniparser/.libs
    champagne_CPPFLAGS += -Iiniparser/src
endif

# freetype hack
champagne_CPPFLAGS += -I/usr/include/freetype2 -I/usr/include
champagne_LDFLAGS += -L/usr/lib -lfreetype
