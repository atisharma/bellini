AUTOMAKE_OPTIONS = foreign

if !SYSTEM_LIBINIPARSER
    SUBDIRS = iniparser
endif

ACLOCAL_AMFLAGS = -I m4

M_CPPFLAGS = -DSYSTEM_LIBINIPARSER=@SYSTEM_LIBINIPARSER@

bin_PROGRAMS = bellini
bellini_SOURCES = bellini.c config.c input/common.c input/fifo.c input/shmem.c \
					sigproc.c \
					output/render.c output/sdlplot.c
bellini_LDFLAGS = -L/usr/local/lib -Wl,-rpath /usr/local/lib
bellini_CPPFLAGS = -DPACKAGE=\"$(PACKAGE)\" -DVERSION=\"$(VERSION)\" \
           -D_POSIX_SOURCE -D _POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE_EXTENDED
bellini_CFLAGS = -std=c99 -Wall -Werror -Wextra -Wno-unused-result -Wno-unknown-warning-option -Wno-maybe-uninitialized

if OSX
    bellini_CFLAGS += -DNORT
else
    bellini_LDFLAGS += -lrt
endif

if ALSA
    bellini_SOURCES += input/alsa.c
endif

if PULSE
    bellini_SOURCES += input/pulse.c
endif

if SNDIO
    bellini_SOURCES += input/sndio.c
endif

if !SYSTEM_LIBINIPARSER
    bellini_LDADD = -liniparser
    bellini_SOURCES += iniparser/libiniparser.la
    bellini_LDADD += -Liniparser/.libs
    bellini_CPPFLAGS += -Iiniparser/src
endif

# freetype hack
bellini_CPPFLAGS += -I/usr/include/freetype2 -I/usr/include
bellini_LDFLAGS += -L/usr/lib -lfreetype

