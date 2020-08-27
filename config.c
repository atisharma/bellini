#include "config.h"
#include "util.h"

#include <ctype.h>
#include <iniparser.h>
#include <math.h>

#ifdef SNDIO
#include <sndio.h>
#endif

#include <stdarg.h>
#include <stdbool.h>
#include <sys/stat.h>

double smoothDef[5] = {1, 1, 1, 1, 1};

enum input_method default_methods[] = {
    INPUT_FIFO,
    INPUT_PORTAUDIO,
    INPUT_ALSA,
    INPUT_PULSE,
};

char *outputMethod, *channels;

const char *input_method_names[] = {
    "fifo", "portaudio", "alsa", "pulse", "sndio", "shmem",
};

const bool has_input_method[] = {
    true, /** Always have at least FIFO and shmem input. */
    HAS_PORTAUDIO, HAS_ALSA, HAS_PULSE, HAS_SNDIO, true,
};

enum input_method input_method_by_name(const char *str) {
    for (int i = 0; i < INPUT_MAX; i++) {
        if (!strcmp(str, input_method_names[i])) {
            return (enum input_method)i;
        }
    }

    return INPUT_MAX;
}

void write_errorf(void *err, const char *fmt, ...) {
    struct error_s *error = (struct error_s *)err;
    va_list args;
    va_start(args, fmt);
    error->length +=
        vsnprintf((char *)error->message + error->length, MAX_ERROR_LEN - error->length, fmt, args);
    va_end(args);
}

int validate_color(char *checkColor) {
    // TODO: take hex colours for the framebuffer display
    int validColor = 0;
    if ((strcmp(checkColor, "black") == 0) || (strcmp(checkColor, "red") == 0) ||
        (strcmp(checkColor, "green") == 0) || (strcmp(checkColor, "yellow") == 0) ||
        (strcmp(checkColor, "blue") == 0) || (strcmp(checkColor, "magenta") == 0) ||
        (strcmp(checkColor, "cyan") == 0) || (strcmp(checkColor, "white") == 0) ||
        (strcmp(checkColor, "default") == 0))
        validColor = 1;
    return validColor;
}

bool validate_colors(void *params, void *err) {
    struct config_params *p = (struct config_params *)params;
    struct error_s *error = (struct error_s *)err;

    // validate: color
    if (!validate_color(p->color)) {
        write_errorf(error, "The value for 'foreground' is invalid. It can be one of the 7 "
                            "named colors.\n");
        return false;
    }

    // validate: background color
    if (!validate_color(p->bcolor)) {
        write_errorf(error, "The value for 'background' is invalid. It can be either one of the 7 "
                            "named colors.\n");
        return false;
    }

    if (p->gradient) {
        for (int i = 0; i < p->gradient_count; i++) {
            if (!validate_color(p->gradient_colors[i])) {
                write_errorf(
                    error,
                    "Gradient color %d is invalid. It must be HTML color of the form '#xxxxxx'.\n",
                    i + 1);
                return false;
            }
        }
    }

    // In case color is not html format set bgcol and col to predefinedint values
    p->col = -1;
    if (strcmp(p->color, "black") == 0)
        p->col = 0;
    if (strcmp(p->color, "red") == 0)
        p->col = 1;
    if (strcmp(p->color, "green") == 0)
        p->col = 2;
    if (strcmp(p->color, "yellow") == 0)
        p->col = 3;
    if (strcmp(p->color, "blue") == 0)
        p->col = 4;
    if (strcmp(p->color, "magenta") == 0)
        p->col = 5;
    if (strcmp(p->color, "cyan") == 0)
        p->col = 6;
    if (strcmp(p->color, "white") == 0)
        p->col = 7;
    // default if invalid

    // validate: background color
    if (strcmp(p->bcolor, "black") == 0)
        p->bgcol = 0;
    if (strcmp(p->bcolor, "red") == 0)
        p->bgcol = 1;
    if (strcmp(p->bcolor, "green") == 0)
        p->bgcol = 2;
    if (strcmp(p->bcolor, "yellow") == 0)
        p->bgcol = 3;
    if (strcmp(p->bcolor, "blue") == 0)
        p->bgcol = 4;
    if (strcmp(p->bcolor, "magenta") == 0)
        p->bgcol = 5;
    if (strcmp(p->bcolor, "cyan") == 0)
        p->bgcol = 6;
    if (strcmp(p->bcolor, "white") == 0)
        p->bgcol = 7;
    // default if invalid

    return true;
}

bool validate_config(struct config_params *p, struct error_s *error) {
    // validate: output method
    p->om = OUTPUT_NOT_SUPORTED;
    if (strcmp(outputMethod, "framebuffer") == 0) {
        p->om = OUTPUT_FRAMEBUFFER;
        p->bgcol = 0;
    }
    if (strcmp(outputMethod, "noncurses") == 0) {
        p->om = OUTPUT_NONCURSES;
        p->bgcol = 0;
    }
    if (strcmp(outputMethod, "raw") == 0) { // raw:
        p->om = OUTPUT_RAW;
        p->bar_spacing = 0;
        p->bar_width = 1;

        // checking data format
        p->is_bin = -1;
        if (strcmp(p->data_format, "binary") == 0) {
            p->is_bin = 1;
            // checking bit format:
            if (p->bit_format != 8 && p->bit_format != 16) {
                write_errorf(
                    error,
                    "bit format  %d is not supported, supported data formats are: '8' and '16'\n",
                    p->bit_format);
                return false;
            }
        } else if (strcmp(p->data_format, "ascii") == 0) {
            p->is_bin = 0;
            if (p->ascii_range < 1) {
                write_errorf(error, "ascii max value must be a positive integer\n");
                return false;
            }
        } else {
            write_errorf(error,
                         "data format %s is not supported, supported data formats are: 'binary' "
                         "and 'ascii'\n",
                         p->data_format);
            return false;
        }
    }
    if (p->om == OUTPUT_NOT_SUPORTED) {
    }

    // force stereo
    p->stereo = 1;

    // validate: colors
    if (!validate_colors(p, error)) {
        return false;
    }

    // validate: alpha
    p->alpha = p->alpha;
    if (p->alpha < 0) {
        p->alpha = 0;
    } else if (p->alpha >= 1.0) {
        p->alpha = 0.99999;
    }

    return true;
}

bool load_colors(struct config_params *p, dictionary *ini, void *err) {
    struct error_s *error = (struct error_s *)err;

    free(p->color);
    free(p->bcolor);

    p->color = strdup(iniparser_getstring(ini, "color:foreground", "default"));
    p->bcolor = strdup(iniparser_getstring(ini, "color:background", "default"));

    p->gradient = iniparser_getint(ini, "color:gradient", 0);
    if (p->gradient) {
        for (int i = 0; i < p->gradient_count; ++i) {
            free(p->gradient_colors[i]);
        }
        p->gradient_count = iniparser_getint(ini, "color:gradient_count", 8);
        if (p->gradient_count < 2) {
            write_errorf(error, "\nAtleast two colors must be given as gradient!\n");
            return false;
        }
        if (p->gradient_count > 8) {
            write_errorf(error, "\nMaximum 8 colors can be specified as gradient!\n");
            return false;
        }
        p->gradient_colors = (char **)malloc(sizeof(char *) * p->gradient_count * 9);
        p->gradient_colors[0] =
            strdup(iniparser_getstring(ini, "color:gradient_color_1", "#59cc33"));
        p->gradient_colors[1] =
            strdup(iniparser_getstring(ini, "color:gradient_color_2", "#80cc33"));
        p->gradient_colors[2] =
            strdup(iniparser_getstring(ini, "color:gradient_color_3", "#a6cc33"));
        p->gradient_colors[3] =
            strdup(iniparser_getstring(ini, "color:gradient_color_4", "#cccc33"));
        p->gradient_colors[4] =
            strdup(iniparser_getstring(ini, "color:gradient_color_5", "#cca633"));
        p->gradient_colors[5] =
            strdup(iniparser_getstring(ini, "color:gradient_color_6", "#cc8033"));
        p->gradient_colors[6] =
            strdup(iniparser_getstring(ini, "color:gradient_color_7", "#cc5933"));
        p->gradient_colors[7] =
            strdup(iniparser_getstring(ini, "color:gradient_color_8", "#cc3333"));
    }
    return true;
}

bool load_config(char configPath[PATH_MAX], struct config_params *p, bool colorsOnly, struct error_s *error) {
    FILE *fp;

    // config: creating path to default config file
    if (configPath[0] == '\0') {
        char *configFile = "config";
        char *configHome = getenv("XDG_CONFIG_HOME");
        if (configHome != NULL) {
            sprintf(configPath, "%s/%s/", configHome, PACKAGE);
        } else {
            configHome = getenv("HOME");
            if (configHome != NULL) {
                sprintf(configPath, "%s/%s/", configHome, ".config");
                mkdir(configPath, 0777);
                sprintf(configPath, "%s/%s/%s/", configHome, ".config", PACKAGE);
            } else {
                write_errorf(error, "No HOME found (ERR_HOMELESS), exiting...");
                return false;
            }
        }

        // config: create directory
        mkdir(configPath, 0777);

        // config: adding default filename file
        strcat(configPath, configFile);

        fp = fopen(configPath, "ab+");
        if (fp) {
            fclose(fp);
        } else {
            write_errorf(error, "Unable to access config '%s', exiting...\n", configPath);
            return false;
        }

    } else { // opening specified file

        fp = fopen(configPath, "rb+");
        if (fp) {
            fclose(fp);
        } else {
            write_errorf(error, "Unable to open file '%s', exiting...\n", configPath);
            return false;
        }
    }

    // config: parse ini
    dictionary *ini;
    ini = iniparser_load(configPath);

    if (colorsOnly) {
        if (!load_colors(p, ini, error)) {
            return false;
        }
        return validate_colors(p, error);
    }

    outputMethod = (char *)iniparser_getstring(ini, "output:method", "noncurses");

    p->alpha = iniparser_getdouble(ini, "smoothing:alpha", 0.8);

    if (!load_colors(p, ini, error)) {
        return false;
    }

    p->fixedbars = iniparser_getint(ini, "general:bars", 0);
    p->bar_width = iniparser_getint(ini, "general:bar_width", 2);
    p->bar_spacing = iniparser_getint(ini, "general:bar_spacing", 1);
    p->framerate = iniparser_getint(ini, "general:framerate", 60);
    p->noise_floor = iniparser_getint(ini, "general:noise_floor", -60);

    // config: output
    free(channels);
    free(p->mono_option);
    free(p->raw_target);
    free(p->data_format);

    channels = strdup(iniparser_getstring(ini, "output:channels", "stereo"));
    p->mono_option = strdup(iniparser_getstring(ini, "output:mono_option", "average"));
    p->raw_target = strdup(iniparser_getstring(ini, "output:raw_target", "/dev/stdout"));
    p->data_format = strdup(iniparser_getstring(ini, "output:data_format", "binary"));
    p->bar_delim = (char)iniparser_getint(ini, "output:bar_delimiter", 59);
    p->frame_delim = (char)iniparser_getint(ini, "output:frame_delimiter", 10);
    p->ascii_range = iniparser_getint(ini, "output:ascii_max_range", 1000);
    p->bit_format = iniparser_getint(ini, "output:bit_format", 16);

    free(p->audio_source);

    char *input_method_name;
    for (size_t i = 0; i < ARRAY_SIZE(default_methods); i++) {
        enum input_method method = default_methods[i];
        if (has_input_method[method]) {
            input_method_name =
                (char *)iniparser_getstring(ini, "input:method", input_method_names[method]);
        }
    }

    p->im = input_method_by_name(input_method_name);
    switch (p->im) {
#ifdef ALSA
    case INPUT_ALSA:
        p->audio_source = strdup(iniparser_getstring(ini, "input:source", "hw:Loopback,1"));
        break;
#endif
    case INPUT_FIFO:
        p->audio_source = strdup(iniparser_getstring(ini, "input:source", "/tmp/mpd.fifo"));
        p->fifoSample = iniparser_getint(ini, "input:sample_rate", 44100);
        p->fifoSampleBits = iniparser_getint(ini, "input:sample_bits", 16);
        break;
#ifdef PULSE
    case INPUT_PULSE:
        p->audio_source = strdup(iniparser_getstring(ini, "input:source", "auto"));
        break;
#endif
#ifdef SNDIO
    case INPUT_SNDIO:
        p->audio_source = strdup(iniparser_getstring(ini, "input:source", SIO_DEVANY));
        break;
#endif
    case INPUT_SHMEM:
        p->audio_source =
            strdup(iniparser_getstring(ini, "input:source", "/squeezelite-00:00:00:00:00:00"));
        break;
#ifdef PORTAUDIO
    case INPUT_PORTAUDIO:
        p->audio_source = strdup(iniparser_getstring(ini, "input:source", "auto"));
        break;
#endif
    case INPUT_MAX: {
        char supported_methods[255] = "";
        for (int i = 0; i < INPUT_MAX; i++) {
            if (has_input_method[i]) {
                strcat(supported_methods, "'");
                strcat(supported_methods, input_method_names[i]);
                strcat(supported_methods, "' ");
            }
        }
        write_errorf(error, "input method '%s' is not supported, supported methods are: %s\n",
                     input_method_name, supported_methods);
        return false;
    }
    default:
        write_errorf(error, "champagne was built without '%s' input support\n",
                     input_method_names[p->im]);
        return false;
    }

    bool result = validate_config(p, error);
    iniparser_freedict(ini);
    return result;
}
