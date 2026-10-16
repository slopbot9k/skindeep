/* The user's settings, as app/settings.js keeps them (decisions 0022,
 * 0035): one list for every skin, which a skin cannot add to, remove
 * from or rename. Each setting overrides one value of the skin's
 * manifest, and unset returns to the skin's own. Beside them, panels
 * keeps the sizes the user dragged each skin's splits to, by skin name
 * and split id. Kept in <state>/settings.json, written at each pick,
 * read at launch. */
#ifndef SK_APP_APPSETTINGS_H
#define SK_APP_APPSETTINGS_H

#include "../base/json.h"
#include "../base/paths.h"

typedef struct {
    const char *id, *label;
    const char *overrides; /* the manifest's value, as a dotted path */
    const char *unit;
    const double *values;  /* the choices, in the menu's order */
    size_t n_values;
    double fallback;       /* the skin's own when its manifest has none */
} sk_setting;

enum { SK_SETTING_FONT_SIZE, SK_SETTINGS_COUNT };

extern const sk_setting sk_settings[SK_SETTINGS_COUNT];

typedef struct {
    char *id;
    double size; /* pixels, finite and not negative; the skin clamps it */
} sk_panel_size;

typedef struct {
    char *skin;
    SK_VEC(sk_panel_size) sizes;
} sk_panel_sizes;

/* Objects keep JavaScript's key order: the settings in the list's order,
 * then panels; skins and ids as Object.assign leaves them. */
typedef struct {
    bool set[SK_SETTINGS_COUNT]; /* false: null, the skin's own */
    double value[SK_SETTINGS_COUNT];
    SK_VEC(sk_panel_sizes) panels;
} sk_appsettings;

/* <state>/settings.json, malloc'd; NULL without a home. */
char *sk_appsettings_file(const sk_env *e);

/* id is a setting and v null or one of its values. v NULL is
 * JavaScript's undefined. */
bool sk_appsettings_valid(const char *id, const sk_json *v);
/* A split's size: a finite number, not negative. */
bool sk_appsettings_size_ok(double v);

/* Every setting, unset where the file has no valid value, and the panel
 * sizes whose skin and id are [a-z0-9-]+ and whose size is valid. A
 * missing or broken file reads as all unset. */
void sk_appsettings_load(sk_appsettings *out, const char *file);
/* The same from the file's text. */
void sk_appsettings_read(sk_appsettings *out, const char *text, size_t n);
/* JSON.stringify(values, null, 1) and a newline. */
void sk_appsettings_write(const sk_appsettings *v, sk_buf *out);
/* Writes the file through a temp file and a rename, making its dir.
 * 0, or -1 with errno. */
int sk_appsettings_save(const char *file, const sk_appsettings *v);
void sk_appsettings_free(sk_appsettings *v);

/* Sets id to value, null unsetting it. False, changing nothing, when
 * sk_appsettings_valid refuses it. */
bool sk_appsettings_set(sk_appsettings *v, const char *id, const sk_json *value);
/* One split of one skin at size, or with id NULL every split of that
 * skin back to the skin's own. The caller checks the split and size. */
void sk_appsettings_set_panel(sk_appsettings *v, const char *skin, const char *id, double size);
/* The kept size of a skin's split. */
bool sk_appsettings_panel(const sk_appsettings *v, const char *skin, const char *id, double *size);

/* The menu's rows: each setting with its choices, the skin's own first
 * as Skin default (14 px), the current one marked. */
typedef struct {
    bool is_default;
    double value;
    char *label;
    bool current;
} sk_setting_choice;

typedef struct {
    const sk_setting *setting;
    SK_VEC(sk_setting_choice) choices;
} sk_setting_row;

/* skin: the current skin's manifest, or NULL. Its value at a setting's
 * path counts when it is a number other than 0, as the JavaScript's ||
 * took it; the skin validator allows nothing else there. */
void sk_appsettings_menu(const sk_appsettings *v, const sk_json *skin,
                         sk_setting_row rows[SK_SETTINGS_COUNT]);
void sk_appsettings_menu_free(sk_setting_row rows[SK_SETTINGS_COUNT]);

#endif
