/* Typed values: each property the four skins and widgets.css use,
 * parsed as a longhand or shorthand and printed back computed. The
 * context: em is 10px, the parent's font 20px, currentcolor red, the
 * parent's colour blue; url(a.png) is image 3 and "spin" keyframes 7. */
#include "../test.h"
#include "util.h"

#include <string.h>

#define V(prop, value, want) CHECK_STR(skt_value(prop, value), want)
#define BAD(prop, value) CHECK(skt_value(prop, value) == NULL)

static void test_colors(void)
{
    V("color", "#3cff3c", "#3cff3c");
    V("color", "#FFF", "#ffffff");
    V("color", "#0f08", "rgba(0, 255, 0, 0.533)");
    V("color", "rgba(60, 255, 60, 0.55)", "rgba(60, 255, 60, 0.55)");
    V("color", "rgb(0 0 0 / 50%)", "rgba(0, 0, 0, 0.5)");
    V("color", "rgb(100%, 0%, 0%)", "#ff0000");
    V("color", "rgb(300, -5, 0)", "#ff0000");
    V("color", "hsl(120, 100%, 50%)", "#00ff00");
    V("color", "hsla(240deg 100% 50% / 0.5)", "rgba(0, 0, 255, 0.5)");
    V("color", "yellow", "#ffff00");
    V("color", "Transparent", "rgba(0, 0, 0, 0)");
    /* currentcolor in color is the parent's; elsewhere the element's. */
    V("color", "currentcolor", "#0000ff");
    V("background-color", "currentColor", "#ff0000");
    BAD("color", "#12345");
    BAD("color", "rgb(1, 2)");
    BAD("color", "rgb(1 2, 3)");
    BAD("color", "rgb(1, 2%, 3)");
    BAD("color", "nocolor");
    BAD("color", "red blue");
}

static void test_lengths(void)
{
    V("width", "calc(50% - 20px)", "calc(50% - 20px)");
    V("top", "calc(100% - 40px)", "calc(100% - 40px)");
    V("left", "calc(2 * (10px + 5%))", "calc(10% + 20px)");
    V("left", "-12px", "-12px");
    V("top", "62%", "62%");
    V("width", "2em", "20px");
    V("width", "1rem", "16px");
    V("width", "1in", "96px");
    V("width", "0", "0px");
    V("margin-top", "auto", "auto");
    V("max-width", "none", "none");
    V("min-height", "0", "0px");
    BAD("width", "-5px");
    BAD("width", "5");
    BAD("width", "none");
    BAD("width", "10vw");
    BAD("width", "calc(10px * 2px)");
    BAD("width", "calc(10px + 5)");
    BAD("width", "calc(50%-20px)");
    /* calc() keeps a negative result, which layout clamps. */
    V("width", "calc(10px - 20px)", "-10px");
    V("stroke-width", "1.5", "1.5px");
    V("letter-spacing", "1.5px", "1.5px");
    V("letter-spacing", "normal", "0px");
    V("letter-spacing", "0.5em", "5px");
    V("inset", "-100px", "-100px | -100px | -100px | -100px");
    V("padding", "1px 5px", "1px | 5px | 1px | 5px");
    V("padding", "1px 8px 0", "1px | 8px | 0px | 8px");
    BAD("padding", "-1px");
    V("margin", "0 auto", "0px | auto | 0px | auto");
}

static void test_fonts(void)
{
    V("font", "bold 22px/24px \"Amatic SC\", cursive", "normal | 700 | 22px | 24px | \"Amatic SC\", \"cursive\" = face 0");
    V("font", "11px/1.2 \"DejaVu Sans Mono\", monospace", "normal | 400 | 11px | 1.2 | \"DejaVu Sans Mono\", \"monospace\" = face 0");
    V("font", "10px monospace", "normal | 400 | 10px | normal | \"monospace\" = face 0");
    V("font", "italic bold 2em Permanent Marker", "italic | 700 | 40px | normal | \"Permanent Marker\" = face 0");
    /* The line height's em is the font size the shorthand gives. */
    V("font", "8px/2em x", "normal | 400 | 8px | 16px | \"x\" = face 0");
    BAD("font", "bold Silkscreen");
    BAD("font", "small-caps 10px x");
    BAD("font", "10px");
    V("font-size", "0.85em", "17px");
    V("font-size", "50%", "10px");
    V("font-size", "larger", "24px");
    V("font-size", "medium", "16px");
    V("font-weight", "bold", "700");
    V("font-weight", "bolder", "700");
    V("font-weight", "lighter", "100");
    V("font-weight", "550", "550");
    BAD("font-weight", "1001");
    V("font-family", "DSEG7, monospace", "\"DSEG7\", \"monospace\" = face 0");
    BAD("font-family", "inherit, x");
    BAD("font-family", "a, 12");
    V("line-height", "normal", "normal");
    V("line-height", "1.2", "1.2");
    V("line-height", "14px", "14px");
    V("line-height", "150%", "15px");
}

static void test_backgrounds(void)
{
    V("background", "#2a1610 url(a.png) 0 0 / 640px 320px",
      "#2a1610 | image(3) | left 0px top 0px | 640px 320px | repeat repeat | padding-box | border-box");
    V("background", "url(a.png) center / cover",
      "rgba(0, 0, 0, 0) | image(3) | left 50% top 50% | cover | repeat repeat | padding-box | border-box");
    V("background", "url(a.png) left bottom / 284px 210px no-repeat",
      "rgba(0, 0, 0, 0) | image(3) | left 0px top 100% | 284px 210px | no-repeat no-repeat | padding-box | border-box");
    V("background", "url(a.png) 0 0 / auto 128px repeat-x",
      "rgba(0, 0, 0, 0) | image(3) | left 0px top 0px | auto 128px | repeat no-repeat | padding-box | border-box");
    V("background",
      "linear-gradient(#e8a848 0 3px, #5a1e12 3px 5px) 0 0 / 100% 100% no-repeat, url(a.png) 11px 17px / 22px 22px, "
      "#9a3e24",
      /* The colour's layer is a layer, its image none, as CSS computes it. */
      "#9a3e24 | linear-gradient(180deg, #e8a848 0px, #e8a848 3px, #5a1e12 3px, #5a1e12 5px), image(3), none | left "
      "0px top 0px, left 11px top 17px, left 0px top 0px | 100% 100%, 22px 22px, auto auto | no-repeat no-repeat, "
      "repeat repeat, repeat repeat | padding-box, padding-box, padding-box | border-box, border-box, border-box");
    BAD("background", "red, url(a.png)");
    BAD("background", "url(a.png) fixed");
    BAD("background", "url(b.png)");
    V("background-size", "auto, 256px 256px", "auto auto, 256px 256px");
    V("background-position", "50% 100%", "left 50% top 100%");
    V("background-position", "right 10px bottom", "right 10px bottom 0px");
    V("background-position", "40px 0", "left 40px top 0px");
    BAD("background-position", "left right");
    V("background-blend-mode", "multiply", "multiply");
    BAD("background-blend-mode", "color-dodge");
    V("background-image", "none, url(a.png)", "none, image(3)");
    V("background-repeat", "repeat-x", "repeat no-repeat");
}

static void test_gradients(void)
{
    V("background-image", "linear-gradient(160deg, rgba(255, 255, 255, 0.16) 0%, rgba(255, 255, 255, 0) 38%)",
      "linear-gradient(160deg, rgba(255, 255, 255, 0.16) 0px, rgba(255, 255, 255, 0) 38%)");
    V("background-image", "linear-gradient(to right, red, blue)", "linear-gradient(90deg, #ff0000, #0000ff)");
    V("background-image", "linear-gradient(to top right, red, blue)", "linear-gradient(to top right, #ff0000, #0000ff)");
    V("background-image", "linear-gradient(0.25turn, red, blue)", "linear-gradient(90deg, #ff0000, #0000ff)");
    V("background-image", "repeating-linear-gradient(90deg, #d89a3e 0 2px, rgba(0, 0, 0, 0) 2px 5px)",
      "repeating-linear-gradient(90deg, #d89a3e 0px, #d89a3e 2px, rgba(0, 0, 0, 0) 2px, rgba(0, 0, 0, 0) 5px)");
    V("background-image", "radial-gradient(ellipse 70% 45% at 50% 0%, red, blue 70%)",
      "radial-gradient(ellipse 70% 45% at 50% 0px, #ff0000, #0000ff 70%)");
    V("background-image", "radial-gradient(circle, #f0bc52 1.8px, rgba(0, 0, 0, 0) 2.3px)",
      "radial-gradient(circle farthest-corner at 50% 50%, #f0bc52 1.8px, rgba(0, 0, 0, 0) 2.3px)");
    V("background-image", "radial-gradient(ellipse at 50% 120%, red 50%, blue)",
      "radial-gradient(ellipse farthest-corner at 50% 120%, #ff0000 50%, #0000ff)");
    V("background-image", "radial-gradient(circle at 38% 32%, #ff9ac6, #f0287a 70%)",
      "radial-gradient(circle farthest-corner at 38% 32%, #ff9ac6, #f0287a 70%)");
    V("background-image", "radial-gradient(10px at right 5px top 0, red, blue)",
      "radial-gradient(circle 10px 10px at calc(100% - 5px) 0px, #ff0000, #0000ff)");
    BAD("background-image", "linear-gradient(red)");
    BAD("background-image", "linear-gradient(red, 10%, blue)");
    BAD("background-image", "radial-gradient(circle 10% 20%, red, blue)");
    BAD("background-image", "conic-gradient(red, blue)");
}

static void test_shadows(void)
{
    V("box-shadow", "inset 0 0 0 1px rgba(255, 220, 170, 0.3), 0 5px 14px rgba(0, 0, 0, 0.6)",
      "inset 0px 0px 0px 1px rgba(255, 220, 170, 0.3), 0px 5px 14px 0px rgba(0, 0, 0, 0.6)");
    V("box-shadow", "0 2px 5px", "0px 2px 5px 0px #ff0000");
    V("box-shadow", "none", "none");
    BAD("box-shadow", "0 0 -2px red");
    BAD("box-shadow", "0 red");
    V("text-shadow", "0 0 4px #ff7a3c, 0 0 10px #ff4f6a", "0px 0px 4px #ff7a3c, 0px 0px 10px #ff4f6a");
    BAD("text-shadow", "inset 0 0 1px red");
    BAD("text-shadow", "0 0 1px 2px red");
}

static void test_transforms(void)
{
    V("transform", "translate(-50%, -50%) rotate(-4deg)", "translate(-50%, -50%) rotate(-4deg)");
    V("transform", "translateY(0) scale(1, 1)", "translate(0px, 0px) scale(1, 1)");
    V("transform", "translateX(-100%)", "translate(-100%, 0px)");
    V("transform", "scale(0.75)", "scale(0.75, 0.75)");
    V("transform", "rotate(0.5turn)", "rotate(180deg)");
    V("transform", "rotate(0)", "rotate(0deg)");
    V("transform", "none", "none");
    V("transform", "skewX(10deg) matrix(1, 0, 0, 1, 5, 6)", "skew(10deg, 0deg) matrix(1, 0, 0, 1, 5, 6)");
    BAD("transform", "rotate(10px)");
    BAD("transform", "rotateX(10deg)");
    BAD("transform", "translate(1px, 2px, 3px)");
    V("transform-origin", "50% 100%", "50% 100%");
    V("transform-origin", "0 0", "0px 0px");
    V("transform-origin", "bottom left", "0px 100%");
    BAD("transform-origin", "0 0 5px");
}

static void test_filters(void)
{
    V("filter", "brightness(0.42) contrast(1.3)", "brightness(0.42) contrast(1.3)");
    V("filter", "drop-shadow(0 3px 2px rgba(0, 0, 0, 0.5))", "drop-shadow(0px 3px 2px rgba(0, 0, 0, 0.5))");
    V("filter", "blur(0.4px)", "blur(0.4px)");
    V("filter", "saturate(110%)", "saturate(1.1)");
    V("filter", "none", "none");
    BAD("filter", "url(#f)");
    BAD("filter", "brightness(-1)");
    V("clip-path", "polygon(7px 0, 33px 0, 40px 50px, 37px 74px, 3px 74px, 0 50px)",
      "polygon(7px 0px, 33px 0px, 40px 50px, 37px 74px, 3px 74px, 0px 50px)");
    V("clip-path", "polygon(evenodd, 0 0, 100% 0, 50% 100%)", "polygon(evenodd, 0px 0px, 100% 0px, 50% 100%)");
    BAD("clip-path", "circle(50%)");
    BAD("clip-path", "polygon(0 0, 1px 1px)");
}

static void test_borders(void)
{
    V("border-radius", "48% 52% 44% 56% / 56% 50% 50% 44%", "48% 56% | 52% 50% | 44% 50% | 56% 44%");
    V("border-radius", "50%", "50% 50% | 50% 50% | 50% 50% | 50% 50%");
    V("border-radius", "8px 8px 3px", "8px 8px | 8px 8px | 3px 3px | 8px 8px");
    V("border-radius", "0 0 3px 3px", "0px 0px | 0px 0px | 3px 3px | 3px 3px");
    BAD("border-radius", "1px / ");
    /* border sets every side, and border-image to its initial value. */
    CHECK(strstr(skt_value("border", "2px solid"), "2px | 2px | 2px | 2px | solid | solid | solid | solid | #ff0000"));
    V("border-color", "#05070a #5c636d #5c636d #05070a", "#05070a | #5c636d | #5c636d | #05070a");
    V("border-bottom", "none", "3px | none | #ff0000");
    V("border-left", "2px solid rgba(58, 255, 126, 0.45)", "2px | solid | rgba(58, 255, 126, 0.45)");
    V("border-width", "thin thick", "1px | 5px | 1px | 5px");
    BAD("border-style", "dotted");
    V("border-image", "url(a.png) 8 fill / 4px stretch",
      "image(3) | 8 8 8 8 fill | 4px 4px 4px 4px | 0px 0px 0px 0px | stretch stretch");
    V("border-image", "url(a.png) 10% 20 / 1 / 2px round space",
      "image(3) | 10% 20 10% 20 | 1 1 1 1 | 2px 2px 2px 2px | round space");
    V("outline", "1px solid red", "1px | solid | #ff0000");
    V("outline", "none", "3px | none | #ff0000");
    V("outline-offset", "-1px", "-1px");
}

static void test_flex_grid(void)
{
    V("flex", "1 1 auto", "1 | 1 | auto");
    V("flex", "1 1 0", "1 | 1 | 0px");
    V("flex", "none", "0 | 0 | auto");
    V("flex", "auto", "1 | 1 | auto");
    V("flex", "1", "1 | 1 | 0px");
    V("flex", "2 0", "2 | 0 | 0px");
    V("flex", "0 0 10px", "0 | 0 | 10px");
    V("flex", "30% 2", "2 | 1 | 30%");
    BAD("flex", "1 2 3");
    BAD("flex", "-1");
    V("gap", "3px", "3px | 3px");
    V("gap", "6px 12px", "6px | 12px");
    V("flex-direction", "row-reverse", "row-reverse");
    V("align-self", "flex-start", "flex-start");
    V("justify-content", "space-between", "space-between");
    BAD("flex-wrap", "wrap");
    V("grid-template-columns", "1fr 1fr", "1fr 1fr");
    V("grid-template-columns", "repeat(2, 1fr) 20px", "1fr 1fr 20px");
    V("grid-auto-rows", "1fr", "1fr");
    V("grid-column", "span 2", "span 2 | auto");
    V("grid-column", "1 / 3", "1 | 3");
    BAD("grid-column", "span 0");
    BAD("grid-template-columns", "minmax(1px, 1fr)");
}

static void test_animation(void)
{
    V("animation", "spin 9.5s ease-in-out -4s infinite",
      "keyframes(7) | 9.5s | cubic-bezier(0.42, 0, 0.58, 1) | -4s | infinite | normal | none | running");
    /* The easing left out is ease. */
    V("animation", "spin 2s", "keyframes(7) | 2s | cubic-bezier(0.25, 0.1, 0.25, 1) | 0s | 1 | normal | none | running");
    V("animation", "spin 4s steps(40) infinite alternate both paused",
      "keyframes(7) | 4s | steps(40, jump-end) | 0s | infinite | alternate | both | paused");
    V("animation", "none", "none | 0s | cubic-bezier(0.25, 0.1, 0.25, 1) | 0s | 1 | normal | none | running");
    BAD("animation", "rise 1s");
    BAD("animation", "spin -1s");
    V("animation-play-state", "paused", "paused");
    V("animation-play-state", "running, paused", "running, paused");
    V("animation-timing-function", "cubic-bezier(0.1, 2, 0.9, -1)", "cubic-bezier(0.1, 2, 0.9, -1)");
    BAD("animation-timing-function", "cubic-bezier(2, 0, 1, 1)");
    V("animation-timing-function", "step-start", "steps(1, jump-start)");
    BAD("animation-timing-function", "steps(1, jump-none)");
    V("transition", "color 0.4s, text-shadow 0.4s",
      "color, text-shadow | 0.4s, 0.4s | cubic-bezier(0.25, 0.1, 0.25, 1), cubic-bezier(0.25, 0.1, 0.25, 1) | 0s, 0s");
    V("transition", "opacity 200ms linear 1s", "opacity | 0.2s | linear | 1s");
    BAD("transition", "background 1s");
    BAD("transition", "nothing 1s");
}

static void test_content(void)
{
    V("content", "\"D E C K\"", "\"D E C K\"");
    V("content", "counter(row) \". \"", "counter(row) \". \"");
    V("content", "\"\"", "\"\"");
    V("content", "none", "none");
    V("content", "counter(task, decimal)", "counter(task)");
    BAD("content", "attr(x)");
    BAD("content", "counter(x, lower-roman)");
    V("counter-reset", "row", "row 0");
    V("counter-reset", "task 3 row", "task 3 row 0");
    V("counter-increment", "row", "row 1");
    V("counter-increment", "none", "none");
}

static void test_keywords(void)
{
    V("display", "grid", "grid");
    BAD("display", "table");
    BAD("display", "inline-flex");
    V("position", "fixed", "fixed");
    BAD("position", "sticky");
    V("overflow", "hidden", "hidden | hidden");
    V("overflow", "visible hidden", "visible | hidden");
    BAD("overflow", "scroll");
    V("opacity", "0.55", "0.55");
    V("opacity", "50%", "0.5");
    V("opacity", "2", "1");
    V("z-index", "2147483647", "2147483647");
    V("z-index", "auto", "auto");
    BAD("z-index", "1.5");
    V("cursor", "pointer", "pointer");
    BAD("cursor", "wait");
    V("white-space", "nowrap", "nowrap");
    BAD("white-space", "pre-wrap");
    V("text-transform", "uppercase", "uppercase");
    V("text-decoration", "line-through", "line-through | #ff0000");
    V("text-overflow", "ellipsis", "ellipsis");
    V("text-align", "center", "center");
    BAD("text-align", "justify");
    V("float", "right", "right");
    V("isolation", "isolate", "isolate");
    V("contain", "none", "none");
    BAD("contain", "strict");
    V("image-rendering", "pixelated", "pixelated");
    V("pointer-events", "none", "none");
    V("visibility", "hidden", "hidden");
    V("user-select", "none", "");
    V("stroke-dasharray", "0.5 1.1", "0.5px 1.1px");
    V("stroke-dasharray", "3, 2", "3px 2px");
    V("stroke-linecap", "round", "round");
    V("fill", "none", "none");
    BAD("fill", "url(#g)");
    V("text-anchor", "middle", "middle");
    V("mix-blend-mode", "screen", "screen");
}

const sk_test sk_suite_skinc_values[] = {
    T(test_colors),     T(test_lengths),  T(test_fonts),    T(test_backgrounds), T(test_gradients),
    T(test_shadows),    T(test_transforms), T(test_filters), T(test_borders),    T(test_flex_grid),
    T(test_animation),  T(test_content),  T(test_keywords), {0},
};
