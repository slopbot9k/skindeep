/* The app's own rules in the skin's shadow root: app/renderer/widgets.css
 * byte for byte (a test compares the two until the cutover removes
 * app/), and the few rules of the browser's user agent sheet the
 * shipped skins rely on: a div is a block, a link is not shown, an svg
 * clips what overflows it. */
#include "front.h"

const char sk_ua_css[] = "div { display: block; }\n"
                         "link { display: none; }\n"
                         "svg { overflow: hidden; }\n";

/* In pieces, each shorter than the 4095 bytes ISO C promises a string. */
static const char *const WIDGETS[] = {
    "/* Structure and defaults of the widgets, loaded in the skin's shadow\n"
    "   root before the skin's own stylesheet, which overrides any of it. */\n"
    ".skin { position: absolute; inset: 0; overflow: hidden; }\n"
    ".panel, .w { position: absolute; box-sizing: border-box; }\n"
    "/* A widget is a display, never text to select: the arrow, not the\n"
    "   I-beam. A widget that takes a click (decision 0033) shows the hand. */\n"
    ".w {\n"
    "  --fill: #4fd06a; --track: #2a2f36; --needle: #ff6a3d; --glow: transparent;\n"
    "  --on: #5cff70; --off: #20302a; --text: #c8d0d8;\n"
    "  color: var(--text); font: 11px/1.2 \"DejaVu Sans Mono\", monospace;\n"
    "  display: flex; flex-direction: column; align-items: center; justify-content: center;\n"
    "  cursor: default; user-select: none;\n"
    "}\n"
    ".w.nodata { opacity: 0.55; }\n"
    ".w.click { cursor: pointer; }\n"
    ".w.click:hover { filter: brightness(1.3); }\n"
    ".w.click.open { outline: 1px solid var(--fill); outline-offset: -1px; }\n"
    ".w .label { text-transform: uppercase; letter-spacing: 1px; white-space: nowrap; }\n"
    ".w .value { white-space: nowrap; }\n"
    ".w .resets { white-space: nowrap; opacity: 0.75; font-size: 0.85em; }\n"
    "\n"
    ".w-gauge .dial, .w-needle .dial { width: 100%; flex: 1 1 auto; min-height: 0; overflow: visible; }\n"
    ".w-gauge .track, .w-needle .track { stroke: var(--track); }\n"
    ".w-gauge .fill { stroke: var(--fill); filter: drop-shadow(0 0 2px var(--glow)); }\n"
    ".w-gauge .tick, .w-needle .tick { stroke: var(--text); stroke-width: 1; opacity: 0.6; }\n"
    ".w-gauge .mark { stroke: var(--needle); stroke-width: 2.5; }\n"
    ".w-gauge text.value { fill: var(--text); font-size: 16px; }\n"
    ".w-needle .track { stroke-width: 2; }\n"
    ".w-needle .needle { stroke: var(--needle); stroke-width: 2; stroke-linecap: round; filter: drop-shadow(0 0 2px "
    "var(--glow)); }\n"
    ".w-needle .pivot { fill: var(--needle); }\n"
    "\n"
    ".w-knob .knob-box { flex: 1 1 auto; width: 100%; min-height: 0; overflow: visible; }\n"
    ".w-knob .knob-face { fill: #2a2f36; stroke: var(--track); stroke-width: 3; }\n"
    ".w-knob .knob-pointer { stroke: var(--fill); stroke-width: 7; stroke-linecap: round; filter: drop-shadow(0 0 2px "
    "var(--glow)); }\n",
    "\n"
    ".w-bar { align-items: stretch; }\n"
    ".w-bar .track { flex: 1 1 auto; background: var(--track); position: relative; overflow: hidden; }\n"
    ".w-bar .fill { position: absolute; left: 0; top: 0; bottom: 0; width: 0; background: var(--fill); box-shadow: 0 "
    "0 6px var(--glow); }\n"
    ".w-bar.vertical .fill { top: auto; right: 0; width: auto; height: 0; }\n"
    ".w-bar .seg { flex: 1 1 0; background: var(--off); }\n"
    ".w-bar .seg.on { background: var(--fill); box-shadow: 0 0 4px var(--glow); }\n"
    "\n"
    ".w-led { flex-direction: row; gap: 6px; }\n"
    ".w-led .lamp { width: 12px; height: 12px; border-radius: 50%; background: var(--off); flex: none; }\n"
    ".w-led .lamp.on { background: var(--on); box-shadow: 0 0 8px var(--on); }\n"
    "\n"
    ".w-readout { align-items: flex-start; }\n"
    ".w-readout .value { font-size: 18px; }\n"
    "\n"
    ".w-sprite .sheet { flex: 1 1 auto; width: 100%; min-height: 0; background-repeat: no-repeat; image-rendering: "
    "pixelated; }\n"
    "\n"
    ".w-list { align-items: stretch; justify-content: flex-start; overflow: hidden; }\n"
    ".w-list.nodata { display: none; }\n"
    ".w-list .items { display: flex; flex-direction: column; flex: 1 1 auto; min-height: 0; overflow: hidden; }\n"
    ".w-list .item { display: flex; flex: none; gap: 6px; align-items: baseline; white-space: nowrap; overflow: hidden; "
    "}\n"
    ".w-list .item .mark { flex: none; }\n"
    ".w-list .item .text { overflow: hidden; text-overflow: ellipsis; }\n"
    ".w-list .item.doing { color: var(--fill); }\n"
    ".w-list .item.done { opacity: 0.55; }\n"
    ".w-list .item.done .text { text-decoration: line-through; }\n"
    "\n"
    "/* The list's toggle (decision 0034): shown while the list has an item,\n"
    "   the list hidden while the tab shows what it replaces. */\n"
    ".skin.checklist-off .w-list { display: none; }\n"
    ".w-toggle { flex-direction: row; gap: 4px; display: none; }\n"
    ".skin.checklist .w-toggle, .skin.checklist-off .w-toggle { display: flex; }\n"
    ".w-toggle .option { cursor: pointer; white-space: nowrap; opacity: 0.6; }\n"
    ".w-toggle .option:hover { opacity: 1; }\n"
    ".w-toggle .option.on { cursor: default; opacity: 1; }\n"
    "\n"
    "/* The usage limits (decision 0011): one meter per limit, sharing the\n"
    "   rect, so an absent limit leaves no gap. */\n"
    ".w-limits { align-items: stretch; justify-content: stretch; gap: 4px; }\n"
    ".w-limits.across { flex-direction: row; }\n"
    ".w-limits.nodata { display: none; }\n"
    ".w-limits > .w { position: relative; flex: 1 1 0; min-width: 0; min-height: 0; }\n"
    ".w-limits > .w .label { overflow: hidden; text-overflow: ellipsis; max-width: 100%; }\n"};

const char *sk_widgets_css(void)
{
    static char *joined;
    if (!joined) {
        sk_buf b = {0};
        for (size_t i = 0; i < SK_LEN(WIDGETS); i++)
            sk_buf_append_c(&b, WIDGETS[i]);
        joined = b.ptr;
    }
    return joined;
}
