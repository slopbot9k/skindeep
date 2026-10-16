/* A skin's manifest (decisions 0005 to 0008, 0024, 0033 to 0035):
 * skin.json checked as app/skins/validate.js checks it, message for
 * message and in its order, then held as the model skinc builds the
 * element tree from. skinc adds its own refusals after validate.js's:
 * files it cannot decode, font weights it cannot read. */
#ifndef SKINC_SKIN_H
#define SKINC_SKIN_H

#include "../src/base/arena.h"
#include "../src/skin/pack.h"
#include "diag.h"

/* validate.js's STRIP: the identity strip above the skin area. */
#define SK_STRIP 22

typedef struct {
    const char *klass; /* "lcd display" */
    sk_pack_rect rect;
} sk_skin_panel;

typedef struct {
    sk_pack_widget w;        /* the spec; strings in the skin's arena */
    sk_pack_rect rect;
    const char *klass;       /* NULL when absent */
    const char *item_class;  /* limits: itemClass */
    const char *face, *image, *sheet; /* files, relative to the skin */
    bool has_toggle;
    sk_pack_rect toggle_rect;
    const char *toggle_labels[2];
    const char *toggle_class;
    /* Fields the manifest gave, for defaults that depend on presence. */
    bool has_min, has_max, has_damping, has_start, has_end, has_thickness, has_gap, has_label, has_format;
} sk_skin_widget;

typedef struct {
    const char *family, *file;
    uint16_t weight;
    uint8_t style; /* sk_font_style */
} sk_skin_font;

typedef struct {
    const char *file;
    uint8_t mode; /* sk_sound_mode */
    float volume, interval[2];
} sk_skin_sound;

typedef struct {
    const char *dir;  /* as given, for messages */
    char *real;       /* its realpath */
    sk_arena arena;
    const char *name, *version, *author, *description, *license;
    float width, height, min_width, min_height;
    sk_pack_rect menu_rect;
    const char *menu_image, *menu_hover, *menu_pressed;
    sk_pack_rect terminal_rect;
    const char *terminal_font; /* NULL: DejaVu Sans Mono */
    float terminal_font_size;
    uint32_t theme[23];
    uint32_t has_theme;
    sk_pack_split *splits;
    size_t nsplits;
    sk_skin_font *fonts;
    size_t nfonts;
    const char *stylesheet; /* relative to the skin, or NULL */
    sk_skin_panel *panels;
    size_t npanels;
    sk_skin_widget *widgets;
    size_t nwidgets;
    sk_skin_sound *sounds;
    size_t nsounds;
} sk_skin;

/* The 23 colours of the terminal's theme, validate.js's THEME_KEYS,
 * then NULL. */
extern const char *const sk_theme_keys[24];

/* Checks dir as validate.js does, then as skinc must. Messages go to
 * d, named for files under dir as given. True, with *skin filled, when
 * there is no error. css receives the stylesheet's text when the
 * manifest names one that exists (the caller frees it), so its
 * @imports and urls can be checked; check_css is called between the
 * fonts' and the panels' checks, where validate.js checks the CSS. */
typedef void (*sk_css_check)(void *data, const char *path, const char *shown, sk_diags *d);
bool sk_skin_load(sk_skin *skin, const char *dir, sk_diags *d, sk_css_check check_css, void *data);
void sk_skin_free(sk_skin *skin);

/* The file check validate.js makes of every path a manifest or a
 * stylesheet names: a relative path to an existing file inside the
 * skin, of one of exts (lowercase, with the dot, NULL-terminated).
 * from: the directory a relative path starts from. The realpath, which
 * the caller frees, or NULL after an error at file, field. */
char *sk_skin_file(const sk_skin *skin, const char *p, const char *field, const char *const *exts, const char *from,
                   const char *file, sk_diags *d);

/* validate.js's extension lists. */
extern const char *const sk_ext_image[];
extern const char *const sk_ext_font[];
extern const char *const sk_ext_audio[];
extern const char *const sk_ext_image_font[];

/* The WCAG contrast of two #rgb or #rrggbb colours, as validate.js
 * computes it. */
double sk_contrast(const char *a, const char *b);
/* JavaScript's x.toFixed(digits) of a finite x. */
void sk_js_fixed(sk_buf *out, double x, int digits);

#endif
