/* xterm.js's custom glyphs: box drawing, block elements, shades, the
 * legacy computing eighths and the Powerline symbols, drawn from their
 * definitions rather than the font, so lines meet across cells. Ported
 * from @xterm/addon-webgl 0.19.0's CustomGlyphs.ts, (c) 2021 The
 * xterm.js authors, MIT licence (third_party/xterm.js/LICENSE).
 *
 * The box drawing and Powerline paths are kept as the SVG path strings
 * CustomGlyphs.ts writes, so the two read side by side. Where it builds
 * a string from xp and yp, the letters a to f stand for its numbers:
 *   a = .5 - xp   b = .5 + xp   c = .5 - yp   d = .5 + yp
 *   e = .5 + (yp / .15 * .5)    f = .5 - (yp / .15 * .5)
 * Coordinates round to the half pixel as translateArgs rounds them, in
 * doubles and one operation at a time, as JavaScript computes them. */
#include "internal.h"

#include <math.h>
#include <stdlib.h>

/* ---- Block elements: rectangles in eighths of the cell ---- */

typedef struct {
    uint32_t cp;
    uint8_t n;
    uint8_t box[8][4]; /* x, y, w, h */
} block_def;

static const block_def blocks[] = {
    {0x2580, 1, {{0, 0, 8, 4}}},
    {0x2581, 1, {{0, 7, 8, 1}}},
    {0x2582, 1, {{0, 6, 8, 2}}},
    {0x2583, 1, {{0, 5, 8, 3}}},
    {0x2584, 1, {{0, 4, 8, 4}}},
    {0x2585, 1, {{0, 3, 8, 5}}},
    {0x2586, 1, {{0, 2, 8, 6}}},
    {0x2587, 1, {{0, 1, 8, 7}}},
    {0x2588, 1, {{0, 0, 8, 8}}},
    {0x2589, 1, {{0, 0, 7, 8}}},
    {0x258A, 1, {{0, 0, 6, 8}}},
    {0x258B, 1, {{0, 0, 5, 8}}},
    {0x258C, 1, {{0, 0, 4, 8}}},
    {0x258D, 1, {{0, 0, 3, 8}}},
    {0x258E, 1, {{0, 0, 2, 8}}},
    {0x258F, 1, {{0, 0, 1, 8}}},
    {0x2590, 1, {{4, 0, 4, 8}}},
    {0x2594, 1, {{0, 0, 8, 1}}},
    {0x2595, 1, {{7, 0, 1, 8}}},
    {0x2596, 1, {{0, 4, 4, 4}}},
    {0x2597, 1, {{4, 4, 4, 4}}},
    {0x2598, 1, {{0, 0, 4, 4}}},
    {0x2599, 2, {{0, 0, 4, 8}, {0, 4, 8, 4}}},
    {0x259A, 2, {{0, 0, 4, 4}, {4, 4, 4, 4}}},
    {0x259B, 2, {{0, 0, 4, 8}, {4, 0, 4, 4}}},
    {0x259C, 2, {{0, 0, 8, 4}, {4, 0, 4, 8}}},
    {0x259D, 1, {{4, 0, 4, 4}}},
    {0x259E, 2, {{4, 0, 4, 4}, {0, 4, 4, 4}}},
    {0x259F, 2, {{4, 0, 4, 8}, {0, 4, 8, 4}}},
    {0x1FB70, 1, {{1, 0, 1, 8}}},
    {0x1FB71, 1, {{2, 0, 1, 8}}},
    {0x1FB72, 1, {{3, 0, 1, 8}}},
    {0x1FB73, 1, {{4, 0, 1, 8}}},
    {0x1FB74, 1, {{5, 0, 1, 8}}},
    {0x1FB75, 1, {{6, 0, 1, 8}}},
    {0x1FB76, 1, {{0, 1, 8, 1}}},
    {0x1FB77, 1, {{0, 2, 8, 1}}},
    {0x1FB78, 1, {{0, 3, 8, 1}}},
    {0x1FB79, 1, {{0, 4, 8, 1}}},
    {0x1FB7A, 1, {{0, 5, 8, 1}}},
    {0x1FB7B, 1, {{0, 6, 8, 1}}},
    {0x1FB7C, 2, {{0, 0, 1, 8}, {0, 7, 8, 1}}},
    {0x1FB7D, 2, {{0, 0, 1, 8}, {0, 0, 8, 1}}},
    {0x1FB7E, 2, {{7, 0, 1, 8}, {0, 0, 8, 1}}},
    {0x1FB7F, 2, {{7, 0, 1, 8}, {0, 7, 8, 1}}},
    {0x1FB80, 2, {{0, 0, 8, 1}, {0, 7, 8, 1}}},
    {0x1FB81, 4, {{0, 0, 8, 1}, {0, 2, 8, 1}, {0, 4, 8, 1}, {0, 7, 8, 1}}},
    {0x1FB82, 1, {{0, 0, 8, 2}}},
    {0x1FB83, 1, {{0, 0, 8, 3}}},
    {0x1FB84, 1, {{0, 0, 8, 5}}},
    {0x1FB85, 1, {{0, 0, 8, 6}}},
    {0x1FB86, 1, {{0, 0, 8, 7}}},
    {0x1FB87, 1, {{6, 0, 2, 8}}},
    {0x1FB88, 1, {{5, 0, 3, 8}}},
    {0x1FB89, 1, {{3, 0, 5, 8}}},
    {0x1FB8A, 1, {{2, 0, 6, 8}}},
    {0x1FB8B, 1, {{1, 0, 7, 8}}},
    {0x1FB95, 8, {{0, 0, 2, 2}, {4, 0, 2, 2}, {2, 2, 2, 2}, {6, 2, 2, 2},
                  {0, 4, 2, 2}, {4, 4, 2, 2}, {2, 6, 2, 2}, {6, 6, 2, 2}}},
    {0x1FB96, 8, {{2, 0, 2, 2}, {6, 0, 2, 2}, {0, 2, 2, 2}, {4, 2, 2, 2},
                  {2, 4, 2, 2}, {6, 4, 2, 2}, {0, 6, 2, 2}, {4, 6, 2, 2}}},
    {0x1FB97, 2, {{0, 2, 8, 2}, {0, 6, 8, 2}}},
};

/* ---- Shades: patterns of device pixels ---- */

static const uint8_t light_bits[] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0};
static const uint8_t medium_bits[] = {1, 0, 0, 0, 0, 1, 0, 0};
static const uint8_t dark_bits[] = {0, 1, 1, 1, 1, 0, 1, 1};
static const sk_ui_pattern light = {4, 4, light_bits}, medium = {2, 4, medium_bits}, dark = {2, 4, dark_bits};

static const sk_ui_pattern *pattern_of(uint32_t cp)
{
    switch (cp) {
    case 0x2591:
        return &light;
    case 0x2592:
        return &medium;
    case 0x2593:
        return &dark;
    default:
        return NULL;
    }
}

/* ---- Box drawing: lines 1 (normal) or 3 (bold) device pixels a scale ---- */

#define TOP_TO_BOTTOM "M.5,0 L.5,1"
#define LEFT_TO_RIGHT "M0,.5 L1,.5"
#define TOP_TO_RIGHT "M.5,0 L.5,.5 L1,.5"
#define TOP_TO_LEFT "M.5,0 L.5,.5 L0,.5"
#define LEFT_TO_BOTTOM "M0,.5 L.5,.5 L.5,1"
#define RIGHT_TO_BOTTOM "M0.5,1 L.5,.5 L1,.5"
#define MIDDLE_TO_TOP "M.5,.5 L.5,0"
#define MIDDLE_TO_LEFT "M.5,.5 L0,.5"
#define MIDDLE_TO_RIGHT "M.5,.5 L1,.5"
#define MIDDLE_TO_BOTTOM "M.5,.5 L.5,1"
#define T_TOP "M0,.5 L1,.5 M.5,.5 L.5,0"
#define T_LEFT "M.5,0 L.5,1 M.5,.5 L0,.5"
#define T_RIGHT "M.5,0 L.5,1 M.5,.5 L1,.5"
#define T_BOTTOM "M0,.5 L1,.5 M.5,.5 L.5,1"
#define CROSS "M0,.5 L1,.5 M.5,0 L.5,1"
#define TWO_DASHES_HORIZONTAL "M.1,.5 L.4,.5 M.6,.5 L.9,.5"
#define THREE_DASHES_HORIZONTAL "M.0667,.5 L.2667,.5 M.4,.5 L.6,.5 M.7333,.5 L.9333,.5"
#define FOUR_DASHES_HORIZONTAL "M.05,.5 L.2,.5 M.3,.5 L.45,.5 M.55,.5 L.7,.5 M.8,.5 L.95,.5"
#define TWO_DASHES_VERTICAL "M.5,.1 L.5,.4 M.5,.6 L.5,.9"
#define THREE_DASHES_VERTICAL "M.5,.0667 L.5,.2667 M.5,.4 L.5,.6 M.5,.7333 L.5,.9333"
/* L, not M, before .55: as xterm.js has it. */
#define FOUR_DASHES_VERTICAL "M.5,.05 L.5,.2 M.5,.3 L.5,.45 L.5,.55 M.5,.7 L.5,.95"

typedef struct {
    uint32_t cp;
    const char *normal, *bold;
} box_def;

static const box_def boxes[] = {
    {0x2500, LEFT_TO_RIGHT, NULL},
    {0x2501, NULL, LEFT_TO_RIGHT},
    {0x2502, TOP_TO_BOTTOM, NULL},
    {0x2503, NULL, TOP_TO_BOTTOM},
    {0x2504, THREE_DASHES_HORIZONTAL, NULL},
    {0x2505, NULL, THREE_DASHES_HORIZONTAL},
    {0x2506, THREE_DASHES_VERTICAL, NULL},
    {0x2507, NULL, THREE_DASHES_VERTICAL},
    {0x2508, FOUR_DASHES_HORIZONTAL, NULL},
    {0x2509, NULL, FOUR_DASHES_HORIZONTAL},
    {0x250A, FOUR_DASHES_VERTICAL, NULL},
    {0x250B, NULL, FOUR_DASHES_VERTICAL},
    {0x250C, RIGHT_TO_BOTTOM, NULL},
    {0x250D, MIDDLE_TO_BOTTOM, MIDDLE_TO_RIGHT},
    {0x250E, MIDDLE_TO_RIGHT, MIDDLE_TO_BOTTOM},
    {0x250F, NULL, RIGHT_TO_BOTTOM},
    {0x2510, LEFT_TO_BOTTOM, NULL},
    {0x2511, MIDDLE_TO_BOTTOM, MIDDLE_TO_LEFT},
    {0x2512, MIDDLE_TO_LEFT, MIDDLE_TO_BOTTOM},
    {0x2513, NULL, LEFT_TO_BOTTOM},
    {0x2514, TOP_TO_RIGHT, NULL},
    {0x2515, MIDDLE_TO_TOP, MIDDLE_TO_RIGHT},
    {0x2516, MIDDLE_TO_RIGHT, MIDDLE_TO_TOP},
    {0x2517, NULL, TOP_TO_RIGHT},
    {0x2518, TOP_TO_LEFT, NULL},
    {0x2519, MIDDLE_TO_TOP, MIDDLE_TO_LEFT},
    {0x251A, MIDDLE_TO_LEFT, MIDDLE_TO_TOP},
    {0x251B, NULL, TOP_TO_LEFT},
    {0x251C, T_RIGHT, NULL},
    {0x251D, TOP_TO_BOTTOM, MIDDLE_TO_RIGHT},
    {0x251E, RIGHT_TO_BOTTOM, MIDDLE_TO_TOP},
    {0x251F, TOP_TO_RIGHT, MIDDLE_TO_BOTTOM},
    {0x2520, MIDDLE_TO_RIGHT, TOP_TO_BOTTOM},
    {0x2521, MIDDLE_TO_BOTTOM, TOP_TO_RIGHT},
    {0x2522, MIDDLE_TO_TOP, RIGHT_TO_BOTTOM},
    {0x2523, NULL, T_RIGHT},
    {0x2524, T_LEFT, NULL},
    {0x2525, TOP_TO_BOTTOM, MIDDLE_TO_LEFT},
    {0x2526, LEFT_TO_BOTTOM, MIDDLE_TO_TOP},
    {0x2527, TOP_TO_LEFT, MIDDLE_TO_BOTTOM},
    {0x2528, MIDDLE_TO_LEFT, TOP_TO_BOTTOM},
    {0x2529, MIDDLE_TO_BOTTOM, TOP_TO_LEFT},
    {0x252A, MIDDLE_TO_TOP, LEFT_TO_BOTTOM},
    {0x252B, NULL, T_LEFT},
    {0x252C, T_BOTTOM, NULL},
    {0x252D, RIGHT_TO_BOTTOM, MIDDLE_TO_LEFT},
    {0x252E, LEFT_TO_BOTTOM, MIDDLE_TO_RIGHT},
    {0x252F, MIDDLE_TO_BOTTOM, LEFT_TO_RIGHT},
    {0x2530, LEFT_TO_RIGHT, MIDDLE_TO_BOTTOM},
    {0x2531, MIDDLE_TO_RIGHT, LEFT_TO_BOTTOM},
    {0x2532, MIDDLE_TO_LEFT, RIGHT_TO_BOTTOM},
    {0x2533, NULL, T_BOTTOM},
    {0x2534, T_TOP, NULL},
    {0x2535, TOP_TO_RIGHT, MIDDLE_TO_LEFT},
    {0x2536, TOP_TO_LEFT, MIDDLE_TO_RIGHT},
    {0x2537, MIDDLE_TO_TOP, LEFT_TO_RIGHT},
    {0x2538, LEFT_TO_RIGHT, MIDDLE_TO_TOP},
    {0x2539, MIDDLE_TO_RIGHT, TOP_TO_LEFT},
    {0x253A, MIDDLE_TO_LEFT, TOP_TO_RIGHT},
    {0x253B, NULL, T_TOP},
    {0x253C, CROSS, NULL},
    {0x253D, TOP_TO_BOTTOM " " MIDDLE_TO_RIGHT, MIDDLE_TO_LEFT},
    {0x253E, TOP_TO_BOTTOM " " MIDDLE_TO_LEFT, MIDDLE_TO_RIGHT},
    {0x253F, TOP_TO_BOTTOM, LEFT_TO_RIGHT},
    {0x2540, LEFT_TO_RIGHT " " MIDDLE_TO_BOTTOM, MIDDLE_TO_TOP},
    {0x2541, MIDDLE_TO_TOP " " LEFT_TO_RIGHT, MIDDLE_TO_BOTTOM},
    {0x2542, LEFT_TO_RIGHT, TOP_TO_BOTTOM},
    {0x2543, RIGHT_TO_BOTTOM, TOP_TO_LEFT},
    {0x2544, LEFT_TO_BOTTOM, TOP_TO_RIGHT},
    {0x2545, TOP_TO_RIGHT, LEFT_TO_BOTTOM},
    {0x2546, TOP_TO_LEFT, RIGHT_TO_BOTTOM},
    {0x2547, MIDDLE_TO_BOTTOM, MIDDLE_TO_TOP " " LEFT_TO_RIGHT},
    {0x2548, MIDDLE_TO_TOP, LEFT_TO_RIGHT " " MIDDLE_TO_BOTTOM},
    {0x2549, MIDDLE_TO_RIGHT, TOP_TO_BOTTOM " " MIDDLE_TO_LEFT},
    {0x254A, MIDDLE_TO_LEFT, TOP_TO_BOTTOM " " MIDDLE_TO_RIGHT},
    {0x254B, NULL, CROSS},
    {0x254C, TWO_DASHES_HORIZONTAL, NULL},
    {0x254D, NULL, TWO_DASHES_HORIZONTAL},
    {0x254E, TWO_DASHES_VERTICAL, NULL},
    {0x254F, NULL, TWO_DASHES_VERTICAL},
    {0x2550, "M0,c L1,c M0,d L1,d", NULL},
    {0x2551, "Ma,0 La,1 Mb,0 Lb,1", NULL},
    {0x2552, "M.5,1 L.5,c L1,c M.5,d L1,d", NULL},
    {0x2553, "Ma,1 La,.5 L1,.5 Mb,.5 Lb,1", NULL},
    {0x2554, "M1,c La,c La,1 M1,d Lb,d Lb,1", NULL},
    {0x2555, "M0,c L.5,c L.5,1 M0,d L.5,d", NULL},
    {0x2556, "Mb,1 Lb,.5 L0,.5 Ma,.5 La,1", NULL},
    {0x2557, "M0,d La,d La,1 M0,c Lb,c Lb,1", NULL},
    {0x2558, "M.5,0 L.5,d L1,d M.5,c L1,c", NULL},
    {0x2559, "M1,.5 La,.5 La,0 Mb,.5 Lb,0", NULL},
    {0x255A, "M1,c Lb,c Lb,0 M1,d La,d La,0", NULL},
    {0x255B, "M0,d L.5,d L.5,0 M0,c L.5,c", NULL},
    {0x255C, "M0,.5 Lb,.5 Lb,0 Ma,.5 La,0", NULL},
    {0x255D, "M0,c La,c La,0 M0,d Lb,d Lb,0", NULL},
    {0x255E, TOP_TO_BOTTOM " M.5,c L1,c M.5,d L1,d", NULL},
    {0x255F, "Ma,0 La,1 Mb,0 Lb,1 Mb,.5 L1,.5", NULL},
    {0x2560, "Ma,0 La,1 M1,d Lb,d Lb,1 M1,c Lb,c Lb,0", NULL},
    {0x2561, TOP_TO_BOTTOM " M0,c L.5,c M0,d L.5,d", NULL},
    {0x2562, "M0,.5 La,.5 Ma,0 La,1 Mb,0 Lb,1", NULL},
    {0x2563, "Mb,0 Lb,1 M0,d La,d La,1 M0,c La,c La,0", NULL},
    {0x2564, "M0,c L1,c M0,d L1,d M.5,d L.5,1", NULL},
    {0x2565, LEFT_TO_RIGHT " Ma,.5 La,1 Mb,.5 Lb,1", NULL},
    {0x2566, "M0,c L1,c M0,d La,d La,1 M1,d Lb,d Lb,1", NULL},
    {0x2567, "M.5,0 L.5,c M0,c L1,c M0,d L1,d", NULL},
    {0x2568, LEFT_TO_RIGHT " Ma,.5 La,0 Mb,.5 Lb,0", NULL},
    {0x2569, "M0,d L1,d M0,c La,c La,0 M1,c Lb,c Lb,0", NULL},
    {0x256A, TOP_TO_BOTTOM " M0,c L1,c M0,d L1,d", NULL},
    {0x256B, LEFT_TO_RIGHT " Ma,0 La,1 Mb,0 Lb,1", NULL},
    {0x256C, "M0,d La,d La,1 M1,d Lb,d Lb,1 M0,c La,c La,0 M1,c Lb,c Lb,0", NULL},
    {0x256D, "M.5,1 L.5,e C.5,e,.5,.5,1,.5", NULL},
    {0x256E, "M.5,1 L.5,e C.5,e,.5,.5,0,.5", NULL},
    {0x256F, "M.5,0 L.5,f C.5,f,.5,.5,0,.5", NULL},
    {0x2570, "M.5,0 L.5,f C.5,f,.5,.5,1,.5", NULL},
    {0x2571, "M1,0 L0,1", NULL},
    {0x2572, "M0,0 L1,1", NULL},
    {0x2573, "M1,0 L0,1 M0,0 L1,1", NULL},
    {0x2574, MIDDLE_TO_LEFT, NULL},
    {0x2575, MIDDLE_TO_TOP, NULL},
    {0x2576, MIDDLE_TO_RIGHT, NULL},
    {0x2577, MIDDLE_TO_BOTTOM, NULL},
    {0x2578, NULL, MIDDLE_TO_LEFT},
    {0x2579, NULL, MIDDLE_TO_TOP},
    {0x257A, NULL, MIDDLE_TO_RIGHT},
    {0x257B, NULL, MIDDLE_TO_BOTTOM},
    {0x257C, MIDDLE_TO_LEFT, MIDDLE_TO_RIGHT},
    {0x257D, MIDDLE_TO_TOP, MIDDLE_TO_BOTTOM},
    {0x257E, MIDDLE_TO_RIGHT, MIDDLE_TO_LEFT},
    {0x257F, MIDDLE_TO_BOTTOM, MIDDLE_TO_TOP},
};

/* ---- Powerline: filled or stroked, clipped to the cell ---- */

typedef struct {
    uint32_t cp;
    bool stroke;
    uint8_t left, right; /* padding, in halves of the line width */
    const char *d;
} powerline_def;

static const powerline_def powerlines[] = {
    {0xE0A0, false, 0, 0,
     "M.3,1 L.03,1 L.03,.88 C.03,.82,.06,.78,.11,.73 C.15,.7,.2,.68,.28,.65 L.43,.6 C.49,.58,.53,.56,.56,.53 "
     "C.59,.5,.6,.47,.6,.43 L.6,.27 L.4,.27 L.69,.1 L.98,.27 L.78,.27 L.78,.46 C.78,.52,.76,.56,.72,.61 "
     "C.68,.66,.63,.67,.56,.7 L.48,.72 C.42,.74,.38,.76,.35,.78 C.32,.8,.31,.84,.31,.88 L.31,1 M.3,.5 L.03,.59 "
     "L.03,.09 L.3,.09 L.3,.655"},
    {0xE0A1, false, 0, 0,
     "M.7,.4 L.7,.47 L.2,.47 L.2,.03 L.355,.03 L.355,.4 L.705,.4 M.7,.5 L.86,.5 L.86,.95 L.69,.95 L.44,.66 "
     "L.46,.86 L.46,.95 L.3,.95 L.3,.49 L.46,.49 L.71,.78 L.69,.565 L.69,.5"},
    {0xE0A2, false, 0, 0,
     "M.25,.94 C.16,.94,.11,.92,.11,.87 L.11,.53 C.11,.48,.15,.455,.23,.45 L.23,.3 C.23,.25,.26,.22,.31,.19 "
     "C.36,.16,.43,.15,.51,.15 C.59,.15,.66,.16,.71,.19 C.77,.22,.79,.26,.79,.3 L.79,.45 C.87,.45,.91,.48,.91,.53 "
     "L.91,.87 C.91,.92,.86,.94,.77,.94 L.24,.94 M.53,.2 C.49,.2,.45,.21,.42,.23 C.39,.25,.38,.27,.38,.3 L.38,.45 "
     "L.68,.45 L.68,.3 C.68,.27,.67,.25,.64,.23 C.61,.21,.58,.2,.53,.2 M.58,.82 L.58,.66 C.63,.65,.65,.63,.65,.6 "
     "C.65,.58,.64,.57,.61,.56 C.58,.55,.56,.54,.52,.54 C.48,.54,.46,.55,.43,.56 C.4,.57,.39,.59,.39,.6 "
     "C.39,.63,.41,.64,.46,.66 L.46,.82 L.57,.82"},
    {0xE0B0, false, 0, 2, "M0,0 L1,.5 L0,1"},
    {0xE0B1, true, 1, 1, "M-1,-.5 L1,.5 L-1,1.5"},
    {0xE0B2, false, 2, 0, "M1,0 L0,.5 L1,1"},
    {0xE0B3, true, 1, 1, "M2,-.5 L0,.5 L2,1.5"},
    {0xE0B4, false, 0, 1, "M0,0 L0,1 C0.552,1,1,0.776,1,.5 C1,0.224,0.552,0,0,0"},
    {0xE0B5, true, 0, 1, "M.2,1 C.422,1,.8,.826,.78,.5 C.8,.174,0.422,0,.2,0"},
    {0xE0B6, false, 1, 0, "M1,0 L1,1 C0.448,1,0,0.776,0,.5 C0,0.224,0.448,0,1,0"},
    {0xE0B7, true, 1, 0, "M.8,1 C0.578,1,0.2,.826,.22,.5 C0.2,0.174,0.578,0,0.8,0"},
    {0xE0B8, false, 0, 0, "M-.5,-.5 L1.5,1.5 L-.5,1.5"},
    {0xE0B9, true, 1, 1, "M-.5,-.5 L1.5,1.5"},
    {0xE0BA, false, 0, 0, "M1.5,-.5 L-.5,1.5 L1.5,1.5"},
    {0xE0BB, true, 1, 1, "M1.5,-.5 L-.5,1.5"},
    {0xE0BC, false, 0, 0, "M1.5,-.5 L-.5,1.5 L-.5,-.5"},
    {0xE0BD, true, 1, 1, "M1.5,-.5 L-.5,1.5"},
    {0xE0BE, false, 0, 0, "M-.5,-.5 L1.5,1.5 L1.5,-.5"},
    {0xE0BF, true, 1, 1, "M-.5,-.5 L1.5,1.5"},
};

static const block_def *find_block(uint32_t cp)
{
    for (size_t i = 0; i < SK_LEN(blocks); i++)
        if (blocks[i].cp == cp)
            return &blocks[i];
    return NULL;
}

static const box_def *find_box(uint32_t cp)
{
    if (cp < 0x2500 || cp > 0x257F)
        return NULL;
    const box_def *b = &boxes[cp - 0x2500];
    return b->cp == cp ? b : NULL;
}

static const powerline_def *find_powerline(uint32_t cp)
{
    for (size_t i = 0; i < SK_LEN(powerlines); i++)
        if (powerlines[i].cp == cp)
            return &powerlines[i];
    return NULL;
}

sk_ui_custom_kind sk_ui_custom_kind_of(uint32_t cp)
{
    if (find_block(cp))
        return SK_UI_CUSTOM_BLOCK;
    if (pattern_of(cp))
        return SK_UI_CUSTOM_PATTERN;
    if (find_box(cp))
        return SK_UI_CUSTOM_BOX;
    if (find_powerline(cp))
        return SK_UI_CUSTOM_POWERLINE;
    return SK_UI_CUSTOM_NONE;
}

/* ---- Paths from the strings ---- */

typedef struct {
    double cell_w, cell_h, x, y;
    bool clamp; /* box drawing's rounding to the half pixel */
    double scale, left, right;
    double vars[6]; /* a to f */
} layout;

static double clampd(double v, double max, double min)
{
    return fmax(fmin(v, max), min);
}

/* translateArgs for one coordinate: along x when x is true. */
static double place(const layout *l, double v, bool x)
{
    double size = x ? l->cell_w : l->cell_h;
    if (x) {
        double pl = l->left * l->scale;
        double pr = l->right * l->scale;
        size -= pl;
        size -= pr;
    }
    double r = v * size;
    if (l->clamp && r != 0) {
        double t = r + 0.5;
        t = sk_ui_js_round(t);
        t -= 0.5;
        r = clampd(t, size, 0);
    }
    if (x) {
        double off = l->left * l->scale;
        off = l->x + off;
        return r + off;
    }
    return r + l->y;
}

/* One argument: a number, or a letter for one of the layout's. */
static double arg(const layout *l, const char *s, const char **end)
{
    if (*s >= 'a' && *s <= 'f') {
        *end = s + 1;
        return l->vars[*s - 'a'];
    }
    char *e;
    double v = strtod(s, &e);
    *end = e;
    return v;
}

/* The instructions of d, M, L and C, separated by spaces. */
static void build(sk_path *p, const char *d, const layout *l)
{
    while (*d) {
        while (*d == ' ')
            d++;
        if (!*d)
            break;
        char type = *d++;
        double v[6] = {0};
        int n = 0;
        while (*d && *d != ' ') {
            const char *e;
            double a = arg(l, d, &e);
            if (e == d)
                break;
            if (n < 6) {
                v[n] = place(l, a, n % 2 == 0);
                n++;
            }
            d = e;
            if (*d == ',')
                d++;
        }
        while (*d && *d != ' ')
            d++;
        if (type == 'M' && n >= 2)
            sk_path_move(p, (float)v[0], (float)v[1]);
        else if (type == 'L' && n >= 2)
            sk_path_line(p, (float)v[0], (float)v[1]);
        else if (type == 'C' && n >= 6)
            sk_path_cubic(p, (float)v[0], (float)v[1], (float)v[2], (float)v[3], (float)v[4], (float)v[5]);
    }
}

bool sk_ui_custom_draw(sk_ui_canvas *cv, uint32_t cp, double x, double y, int w, int h, double font_px,
                       double scale)
{
    const block_def *blk = find_block(cp);
    if (blk) {
        double xe = (double)w / 8, ye = (double)h / 8;
        for (int i = 0; i < blk->n; i++) {
            const uint8_t *b = blk->box[i];
            sk_ui_canvas_rect(cv, x + b[0] * xe, y + b[1] * ye, b[2] * xe, b[3] * ye, cv->fill);
        }
        return true;
    }
    const sk_ui_pattern *pat = pattern_of(cp);
    if (pat) {
        cv->fill.pattern = pat;
        sk_ui_canvas_rect(cv, x, y, w, h, cv->fill);
        return true;
    }
    layout l = {(double)w, (double)h, x, y, true, scale, 0, 0, {0}};
    const box_def *box = find_box(cp);
    if (box) {
        double xp = .15;
        double yp = .15 / (double)h;
        yp *= (double)w;
        double k = yp / .15;
        k *= .5;
        l.vars[0] = .5 - xp;
        l.vars[1] = .5 + xp;
        l.vars[2] = .5 - yp;
        l.vars[3] = .5 + yp;
        l.vars[4] = .5 + k;
        l.vars[5] = .5 - k;
        /* Normal, then bold: Object.entries orders the weights so. */
        const char *defs[2] = {box->normal, box->bold};
        for (int i = 0; i < 2; i++) {
            if (!defs[i])
                continue;
            cv->line_width = scale * (i ? 3 : 1);
            sk_path *p = sk_path_new();
            build(p, defs[i], &l);
            sk_ui_canvas_stroke(cv, p, cv->line_width, cv->fill, NULL);
            sk_path_free(p);
        }
        return true;
    }
    const powerline_def *pl = find_powerline(cp);
    if (pl) {
        double r[4] = {x, y, x + w, y + h};
        if (cv->clipped) {
            r[0] = fmax(r[0], cv->clip[0]);
            r[1] = fmax(r[1], cv->clip[1]);
            r[2] = fmin(r[2], cv->clip[2]);
            r[3] = fmin(r[3], cv->clip[3]);
        }
        cv->clipped = true;
        for (int i = 0; i < 4; i++)
            cv->clip[i] = r[i];
        double css_line = font_px / 12;
        cv->line_width = scale * css_line;
        l.clamp = false;
        l.left = pl->left * (css_line / 2);
        l.right = pl->right * (css_line / 2);
        sk_path *p = sk_path_new();
        build(p, pl->d, &l);
        if (pl->stroke)
            sk_ui_canvas_stroke(cv, p, cv->line_width, cv->fill, NULL);
        else
            sk_ui_canvas_fill(cv, p, cv->fill, NULL);
        sk_path_free(p);
        return true;
    }
    return false;
}

/* The canvas's padding around a cell in the atlas, which a glyph's
 * strokes may reach into. */
#define PAD 4

/* xterm.js's 14 px, which sets the Powerline glyphs' line width. */
#define DEFAULT_FONT_PX 14

bool sk_termview_custom_glyph(sk_image *dst, sk_rect r, uint32_t cp, sk_color c, double scale)
{
    if (sk_ui_custom_kind_of(cp) == SK_UI_CUSTOM_NONE)
        return false;
    if (sk_rect_empty(r) || r.w > INT32_MAX / 2 - PAD || r.h > INT32_MAX / 2 - PAD)
        return true;
    sk_ui_canvas cv;
    sk_ui_canvas_init(&cv, r.w + 2 * PAD, r.h + 2 * PAD, 0);
    cv.fill.c = c;
    sk_ui_custom_draw(&cv, cp, PAD, PAD, r.w, r.h, DEFAULT_FONT_PX, scale);
    sk_blit(dst, r.x - PAD, r.y - PAD, &cv.im, sk_image_bounds(&cv.im), SK_BLEND_SRC_OVER, 255, NULL);
    sk_ui_canvas_free(&cv);
    return true;
}
