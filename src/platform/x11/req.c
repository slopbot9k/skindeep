/* The requests, encoded as the protocol's encoding appendix lays them
 * out, each its own function so a test can check its bytes. */
#include "x11.h"

#include <string.h>

static uint64_t empty(sk_x11 *c, uint8_t opcode)
{
    uint64_t s = sk_x11_begin(c, opcode, 0);
    sk_x11_end(c);
    return s;
}

static uint64_t with_id(sk_x11 *c, uint8_t opcode, uint32_t id)
{
    uint64_t s = sk_x11_begin(c, opcode, 0);
    sk_x11_put32(c, id);
    sk_x11_end(c);
    return s;
}

static uint64_t with_name(sk_x11 *c, uint8_t opcode, uint8_t data, const char *name)
{
    size_t n = strlen(name);
    uint64_t s = sk_x11_begin(c, opcode, data);
    sk_x11_put16(c, (uint32_t)n);
    sk_x11_put16(c, 0);
    sk_x11_put(c, name, n);
    sk_x11_end(c);
    return s;
}

static void put_values(sk_x11 *c, uint32_t mask, const uint32_t *values)
{
    sk_x11_put32(c, mask);
    for (uint32_t m = mask, i = 0; m; m &= m - 1, i++)
        sk_x11_put32(c, values[i]);
}

uint64_t sk_x11_create_window(sk_x11 *c, uint8_t depth, uint32_t wid, uint32_t parent, int x, int y,
                              int w, int h, uint16_t cls, uint32_t visual, uint32_t mask,
                              const uint32_t *values)
{
    uint64_t s = sk_x11_begin(c, 1, depth);
    sk_x11_put32(c, wid);
    sk_x11_put32(c, parent);
    sk_x11_put16(c, (uint32_t)x);
    sk_x11_put16(c, (uint32_t)y);
    sk_x11_put16(c, (uint32_t)w);
    sk_x11_put16(c, (uint32_t)h);
    sk_x11_put16(c, 0); /* border width */
    sk_x11_put16(c, cls);
    sk_x11_put32(c, visual);
    put_values(c, mask, values);
    sk_x11_end(c);
    return s;
}

uint64_t sk_x11_change_attributes(sk_x11 *c, uint32_t win, uint32_t mask, const uint32_t *values)
{
    uint64_t s = sk_x11_begin(c, 2, 0);
    sk_x11_put32(c, win);
    put_values(c, mask, values);
    sk_x11_end(c);
    return s;
}

uint64_t sk_x11_destroy_window(sk_x11 *c, uint32_t win)
{
    return with_id(c, 4, win);
}

uint64_t sk_x11_map_window(sk_x11 *c, uint32_t win)
{
    return with_id(c, 8, win);
}

uint64_t sk_x11_unmap_window(sk_x11 *c, uint32_t win)
{
    return with_id(c, 10, win);
}

uint64_t sk_x11_configure_window(sk_x11 *c, uint32_t win, uint16_t mask, const uint32_t *values)
{
    uint64_t s = sk_x11_begin(c, 12, 0);
    sk_x11_put32(c, win);
    sk_x11_put16(c, mask);
    sk_x11_put16(c, 0);
    for (uint32_t m = mask, i = 0; m; m &= m - 1, i++)
        sk_x11_put32(c, values[i]);
    sk_x11_end(c);
    return s;
}

uint64_t sk_x11_intern_atom(sk_x11 *c, const char *name, bool only_if_exists)
{
    return with_name(c, 16, only_if_exists, name);
}

uint64_t sk_x11_change_property(sk_x11 *c, uint8_t mode, uint32_t win, uint32_t prop, uint32_t type,
                                uint8_t format, const void *data, uint32_t n)
{
    uint64_t s = sk_x11_begin(c, 18, mode);
    sk_x11_put32(c, win);
    sk_x11_put32(c, prop);
    sk_x11_put32(c, type);
    sk_x11_put8(c, format);
    sk_x11_put8(c, 0);
    sk_x11_put16(c, 0);
    sk_x11_put32(c, n);
    sk_x11_put(c, data, (size_t)n * (format / 8));
    sk_x11_end(c);
    return s;
}

uint64_t sk_x11_delete_property(sk_x11 *c, uint32_t win, uint32_t prop)
{
    uint64_t s = sk_x11_begin(c, 19, 0);
    sk_x11_put32(c, win);
    sk_x11_put32(c, prop);
    sk_x11_end(c);
    return s;
}

uint64_t sk_x11_get_property(sk_x11 *c, bool del, uint32_t win, uint32_t prop, uint32_t type,
                             uint32_t offset, uint32_t length)
{
    uint64_t s = sk_x11_begin(c, 20, del);
    sk_x11_put32(c, win);
    sk_x11_put32(c, prop);
    sk_x11_put32(c, type);
    sk_x11_put32(c, offset);
    sk_x11_put32(c, length);
    sk_x11_end(c);
    return s;
}

uint64_t sk_x11_set_selection_owner(sk_x11 *c, uint32_t owner, uint32_t selection, uint32_t time)
{
    uint64_t s = sk_x11_begin(c, 22, 0);
    sk_x11_put32(c, owner);
    sk_x11_put32(c, selection);
    sk_x11_put32(c, time);
    sk_x11_end(c);
    return s;
}

uint64_t sk_x11_convert_selection(sk_x11 *c, uint32_t requestor, uint32_t selection,
                                  uint32_t target, uint32_t property, uint32_t time)
{
    uint64_t s = sk_x11_begin(c, 24, 0);
    sk_x11_put32(c, requestor);
    sk_x11_put32(c, selection);
    sk_x11_put32(c, target);
    sk_x11_put32(c, property);
    sk_x11_put32(c, time);
    sk_x11_end(c);
    return s;
}

uint64_t sk_x11_send_event(sk_x11 *c, bool propagate, uint32_t dest, uint32_t mask,
                           const uint8_t ev[32])
{
    uint64_t s = sk_x11_begin(c, 25, propagate);
    sk_x11_put32(c, dest);
    sk_x11_put32(c, mask);
    sk_x11_put(c, ev, 32);
    sk_x11_end(c);
    return s;
}

uint64_t sk_x11_set_input_focus(sk_x11 *c, uint8_t revert_to, uint32_t focus, uint32_t time)
{
    uint64_t s = sk_x11_begin(c, 42, revert_to);
    sk_x11_put32(c, focus);
    sk_x11_put32(c, time);
    sk_x11_end(c);
    return s;
}

uint64_t sk_x11_translate_coordinates(sk_x11 *c, uint32_t src, uint32_t dst, int x, int y)
{
    uint64_t s = sk_x11_begin(c, 40, 0);
    sk_x11_put32(c, src);
    sk_x11_put32(c, dst);
    sk_x11_put16(c, (uint32_t)(uint16_t)(int16_t)x);
    sk_x11_put16(c, (uint32_t)(uint16_t)(int16_t)y);
    sk_x11_end(c);
    return s;
}

uint64_t sk_x11_get_input_focus(sk_x11 *c)
{
    return empty(c, 43);
}

uint64_t sk_x11_open_font(sk_x11 *c, uint32_t fid, const char *name)
{
    size_t n = strlen(name);
    uint64_t s = sk_x11_begin(c, 45, 0);
    sk_x11_put32(c, fid);
    sk_x11_put16(c, (uint32_t)n);
    sk_x11_put16(c, 0);
    sk_x11_put(c, name, n);
    sk_x11_end(c);
    return s;
}

uint64_t sk_x11_close_font(sk_x11 *c, uint32_t fid)
{
    return with_id(c, 46, fid);
}

uint64_t sk_x11_create_pixmap(sk_x11 *c, uint8_t depth, uint32_t pid, uint32_t drawable, int w,
                              int h)
{
    uint64_t s = sk_x11_begin(c, 53, depth);
    sk_x11_put32(c, pid);
    sk_x11_put32(c, drawable);
    sk_x11_put16(c, (uint32_t)w);
    sk_x11_put16(c, (uint32_t)h);
    sk_x11_end(c);
    return s;
}

uint64_t sk_x11_free_pixmap(sk_x11 *c, uint32_t pid)
{
    return with_id(c, 54, pid);
}

uint64_t sk_x11_create_gc(sk_x11 *c, uint32_t gc, uint32_t drawable, uint32_t mask,
                          const uint32_t *values)
{
    uint64_t s = sk_x11_begin(c, 55, 0);
    sk_x11_put32(c, gc);
    sk_x11_put32(c, drawable);
    put_values(c, mask, values);
    sk_x11_end(c);
    return s;
}

uint64_t sk_x11_free_gc(sk_x11 *c, uint32_t gc)
{
    return with_id(c, 60, gc);
}

uint64_t sk_x11_fill_rect(sk_x11 *c, uint32_t drawable, uint32_t gc, int x, int y, int w, int h)
{
    uint64_t s = sk_x11_begin(c, 70, 0);
    sk_x11_put32(c, drawable);
    sk_x11_put32(c, gc);
    sk_x11_put16(c, (uint32_t)x);
    sk_x11_put16(c, (uint32_t)y);
    sk_x11_put16(c, (uint32_t)w);
    sk_x11_put16(c, (uint32_t)h);
    sk_x11_end(c);
    return s;
}

uint64_t sk_x11_put_image(sk_x11 *c, uint32_t drawable, uint32_t gc, int x, int y, int w, int h,
                          uint8_t depth, const uint32_t *px, size_t stride, bool swap)
{
    uint64_t s = sk_x11_begin(c, 72, 2 /* ZPixmap */);
    sk_x11_put32(c, drawable);
    sk_x11_put32(c, gc);
    sk_x11_put16(c, (uint32_t)w);
    sk_x11_put16(c, (uint32_t)h);
    sk_x11_put16(c, (uint32_t)x);
    sk_x11_put16(c, (uint32_t)y);
    sk_x11_put8(c, 0); /* left pad */
    sk_x11_put8(c, depth);
    sk_x11_put16(c, 0);
    size_t row = (size_t)w * 4;
    sk_buf_reserve(&c->out, row * (size_t)h);
    for (int r = 0; r < h; r++) {
        const uint32_t *src = px + (size_t)r * stride;
        uint8_t *dst = (uint8_t *)c->out.ptr + c->out.len;
        memcpy(dst, src, row);
        if (swap) {
            for (size_t i = 0; i < row; i += 4) {
                uint8_t t = dst[i];
                dst[i] = dst[i + 3];
                dst[i + 3] = t;
                t = dst[i + 1];
                dst[i + 1] = dst[i + 2];
                dst[i + 2] = t;
            }
        }
        c->out.len += row;
    }
    c->out.ptr[c->out.len] = 0;
    sk_x11_end(c);
    return s;
}

uint64_t sk_x11_get_image(sk_x11 *c, uint32_t drawable, int x, int y, int w, int h)
{
    uint64_t s = sk_x11_begin(c, 73, 2 /* ZPixmap */);
    sk_x11_put32(c, drawable);
    sk_x11_put16(c, (uint32_t)x);
    sk_x11_put16(c, (uint32_t)y);
    sk_x11_put16(c, (uint32_t)w);
    sk_x11_put16(c, (uint32_t)h);
    sk_x11_put32(c, 0xffffffff); /* plane mask */
    sk_x11_end(c);
    return s;
}

uint64_t sk_x11_create_colormap(sk_x11 *c, uint32_t mid, uint32_t win, uint32_t visual)
{
    uint64_t s = sk_x11_begin(c, 78, 0 /* alloc None */);
    sk_x11_put32(c, mid);
    sk_x11_put32(c, win);
    sk_x11_put32(c, visual);
    sk_x11_end(c);
    return s;
}

uint64_t sk_x11_free_colormap(sk_x11 *c, uint32_t mid)
{
    return with_id(c, 79, mid);
}

uint64_t sk_x11_create_glyph_cursor(sk_x11 *c, uint32_t cid, uint32_t font, uint16_t ch)
{
    uint64_t s = sk_x11_begin(c, 94, 0);
    sk_x11_put32(c, cid);
    sk_x11_put32(c, font);
    sk_x11_put32(c, font);
    sk_x11_put16(c, ch);
    sk_x11_put16(c, (uint32_t)ch + 1);
    for (int i = 0; i < 3; i++)
        sk_x11_put16(c, 0);
    for (int i = 0; i < 3; i++)
        sk_x11_put16(c, 0xffff);
    sk_x11_end(c);
    return s;
}

uint64_t sk_x11_create_cursor(sk_x11 *c, uint32_t cid, uint32_t source, uint32_t mask)
{
    uint64_t s = sk_x11_begin(c, 93, 0);
    sk_x11_put32(c, cid);
    sk_x11_put32(c, source);
    sk_x11_put32(c, mask);
    for (int i = 0; i < 8; i++)
        sk_x11_put16(c, 0); /* black on black, the hot spot at (0, 0) */
    sk_x11_end(c);
    return s;
}

uint64_t sk_x11_free_cursor(sk_x11 *c, uint32_t cid)
{
    return with_id(c, 95, cid);
}

uint64_t sk_x11_query_extension(sk_x11 *c, const char *name)
{
    return with_name(c, 98, 0, name);
}

uint64_t sk_x11_get_keyboard_mapping(sk_x11 *c, uint8_t first, uint8_t count)
{
    uint64_t s = sk_x11_begin(c, 101, 0);
    sk_x11_put8(c, first);
    sk_x11_put8(c, count);
    sk_x11_put16(c, 0);
    sk_x11_end(c);
    return s;
}

uint64_t sk_x11_get_modifier_mapping(sk_x11 *c)
{
    return empty(c, 119);
}

uint64_t sk_x11_big_enable(sk_x11 *c)
{
    return empty(c, c->big_opcode);
}

uint64_t sk_x11_shm_query_version(sk_x11 *c)
{
    return empty(c, c->shm_opcode);
}

uint64_t sk_x11_shm_attach(sk_x11 *c, uint32_t seg, uint32_t shmid, bool read_only)
{
    uint64_t s = sk_x11_begin(c, c->shm_opcode, 1);
    sk_x11_put32(c, seg);
    sk_x11_put32(c, shmid);
    sk_x11_put8(c, read_only);
    sk_x11_end(c);
    return s;
}

uint64_t sk_x11_shm_detach(sk_x11 *c, uint32_t seg)
{
    uint64_t s = sk_x11_begin(c, c->shm_opcode, 2);
    sk_x11_put32(c, seg);
    sk_x11_end(c);
    return s;
}

uint64_t sk_x11_shm_put_image(sk_x11 *c, uint32_t drawable, uint32_t gc, int total_w, int total_h,
                              int src_x, int src_y, int w, int h, int dst_x, int dst_y,
                              uint8_t depth, bool send_event, uint32_t seg, uint32_t offset)
{
    uint64_t s = sk_x11_begin(c, c->shm_opcode, 3);
    sk_x11_put32(c, drawable);
    sk_x11_put32(c, gc);
    sk_x11_put16(c, (uint32_t)total_w);
    sk_x11_put16(c, (uint32_t)total_h);
    sk_x11_put16(c, (uint32_t)src_x);
    sk_x11_put16(c, (uint32_t)src_y);
    sk_x11_put16(c, (uint32_t)w);
    sk_x11_put16(c, (uint32_t)h);
    sk_x11_put16(c, (uint32_t)dst_x);
    sk_x11_put16(c, (uint32_t)dst_y);
    sk_x11_put8(c, depth);
    sk_x11_put8(c, 2 /* ZPixmap */);
    sk_x11_put8(c, send_event);
    sk_x11_put8(c, 0);
    sk_x11_put32(c, seg);
    sk_x11_put32(c, offset);
    sk_x11_end(c);
    return s;
}
