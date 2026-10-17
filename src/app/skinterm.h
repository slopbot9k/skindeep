/* A skin's terminal as the terminals take it (skin.js's styleTerminal,
 * decision 0006): the theme's colours over xterm.js's defaults for the
 * palette every tab's terminal is set to, the selection and cursor
 * colours and the font size for the views, and the font set of the
 * skin's face or DejaVu Sans Mono. The UI thread's, as the views are. */
#ifndef SK_APP_SKINTERM_H
#define SK_APP_SKINTERM_H

#include "../skin/view.h"
#include "../term/term.h"
#include "../ui/termview.h"

/* t's colours into pal (sk_term_set_palette's), each the default where
 * the theme gives none, and into st's colours; st's font size the
 * user's setting (0 when unset) over the skin's. st's fonts, cache and
 * scale are left as they are. */
void sk_skin_term_style(const sk_skin_terminal *t, double setting, sk_term_palette *pal, sk_termview_style *st);
/* The terminals' font set: the skin's face first, else DejaVu Sans
 * Mono, then fs's faces for what it lacks. */
sk_fontset *sk_skin_term_fonts(const sk_skin_terminal *t, sk_fonts *fs);

#endif
