#include "skinterm.h"

static bool has(const sk_skin_terminal *t, int key)
{
    return t->has & (1u << key);
}

void sk_skin_term_style(const sk_skin_terminal *t, double setting, sk_term_palette *pal, sk_termview_style *st)
{
    sk_term_default_palette(pal);
    for (int i = 0; i < 16; i++)
        if (has(t, SK_THEME_BLACK + i))
            pal->ansi[i] = t->colors[SK_THEME_BLACK + i] & 0xffffffu;
    if (has(t, SK_THEME_FOREGROUND))
        pal->fg = t->colors[SK_THEME_FOREGROUND] & 0xffffffu;
    if (has(t, SK_THEME_BACKGROUND))
        pal->bg = t->colors[SK_THEME_BACKGROUND] & 0xffffffu;
    if (has(t, SK_THEME_CURSOR))
        pal->cursor = t->colors[SK_THEME_CURSOR] & 0xffffffu;
    sk_termview_style d;
    sk_termview_default_style(&d);
    st->selection_bg = has(t, SK_THEME_SELECTION_BACKGROUND) ? t->colors[SK_THEME_SELECTION_BACKGROUND] : d.selection_bg;
    st->selection_inactive_bg = has(t, SK_THEME_SELECTION_INACTIVE_BACKGROUND)
                                    ? t->colors[SK_THEME_SELECTION_INACTIVE_BACKGROUND]
                                    : st->selection_bg;
    st->selection_fg = has(t, SK_THEME_SELECTION_FOREGROUND) ? t->colors[SK_THEME_SELECTION_FOREGROUND] : 0;
    st->cursor_accent = has(t, SK_THEME_CURSOR_ACCENT) ? t->colors[SK_THEME_CURSOR_ACCENT] : d.cursor_accent;
    st->font_px = sk_skin_font_px(t, setting);
}

sk_fontset *sk_skin_term_fonts(const sk_skin_terminal *t, sk_fonts *fs)
{
    return sk_fontset_new_face(fs, t->face, NULL, true);
}
