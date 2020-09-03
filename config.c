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

enum input_method default_methods[] = {
    INPUT_FIFO,
    INPUT_PORTAUDIO,
    INPUT_ALSA,
    INPUT_PULSE,
};

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
    int validColor = 0;
    if (checkColor[0] == '#' && strlen(checkColor) == 7) {
        // 0 to 9 and a to f
        for (int i = 1; checkColor[i]; ++i) {
            if (!isdigit(checkColor[i])) {
                if (tolower(checkColor[i]) >= 'a' && tolower(checkColor[i]) <= 'f') {
                    validColor = 1;
                } else {
                    validColor = 0;
                    break;
                }
            } else {
                validColor = 1;
            }
        }
    }
    return validColor;
}

bool validate_colors(void *params, void *err) {
    struct config_params *p = (struct config_params *)params;
    struct error_s *error = (struct error_s *)err;

    // validate: color
    if (
            (!validate_color(p->plot_l_col)) ||
            (!validate_color(p->plot_r_col)) ||
            (!validate_color(p->ax_col)) ||
            (!validate_color(p->ax_2_col)) ||
            (!validate_color(p->text_col)) ||
            (!validate_color(p->audio_col))
            ) {
        write_errorf(error, "The value for some color is invalid. It can be one of the 7 "
                            "named colors.\n");
        return false;
    }

    return true;
}

bool validate_config(struct config_params *p, struct error_s *error) {
    // validate: colors
    if (!validate_colors(p, error)) {
        return false;
    }

    // validate: alpha
    if (p->alpha < 0) {
        p->alpha = 0;
    } else if (p->alpha >= 1.0) {
        p->alpha = 0.99999;
    }

    return true;
}

bool load_colors(struct config_params *p, dictionary *ini) {

    free(p->plot_l_col);
    p->plot_l_col = strdup(iniparser_getstring(ini, "color:plot_l", "#FF5100"));

    free(p->plot_r_col);
    p->plot_r_col = strdup(iniparser_getstring(ini, "color:plot_r", "#00FF61"));
    
    free(p->ax_col);
    p->ax_col = strdup(iniparser_getstring(ini, "color:ax", "#92FF00"));

    free(p->ax_2_col);
    p->ax_2_col = strdup(iniparser_getstring(ini, "color:ax_2", "#FF5110"));

    free(p->text_col);
    p->text_col = strdup(iniparser_getstring(ini, "color:text", "#FF5000"));

    free(p->audio_col);
    p->audio_col = strdup(iniparser_getstring(ini, "color:audio", "#FF5000"));

    return true;
}

bool load_config(char configPath[PATH_MAX], struct config_params *p, struct error_s *error) {
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

    // config: general
    p->alpha = iniparser_getdouble(ini, "general:alpha", 0.95);

    if (!load_colors(p, ini)) {
        return false;
    }

    p->noise_floor = iniparser_getint(ini, "general:noise_floor", -100);
    
    free(p->text_font);
    p->text_font = strdup(iniparser_getstring(ini, "general:text_font", "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf"));

    free(p->audio_font);
    p->audio_font = strdup(iniparser_getstring(ini, "general:audio_font", "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf"));

    free(p->vis);
    p->vis = strdup(iniparser_getstring(ini, "general:vis", "fft"));

    // config: output
    free(p->audio_source);

    // config: input
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
