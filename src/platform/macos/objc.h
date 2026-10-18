/* AppKit from C (decision 0036): its headers are Objective-C, so the
 * window layer reaches it through the Objective-C runtime. Every
 * message is objc_msgSend cast to the exact prototype of its method, as
 * arm64 requires; a method returning a struct larger than 16 bytes goes
 * through objc_msgSend_stret on x86_64. NSRect, NSPoint and NSSize are
 * CGRect, CGPoint and CGSize, passed and returned by value.
 *
 * The constants below are AppKit's, copied from its headers under
 * Apple's names; the externs are AppKit's, QuartzCore's and libobjc's
 * own symbols. */
#ifndef SK_MACOS_OBJC_H
#define SK_MACOS_OBJC_H

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <limits.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <stdlib.h>

/* objc_msgSend as a function of type T, as in
 * SK_MSG(void (*)(id, SEL, CGSize))(win, sk_sel("setContentSize:"), size). */
#define SK_MSG(T) ((T)objc_msgSend)
#define SK_MSG_SUPER(T) ((T)objc_msgSendSuper)
#if defined(__x86_64__)
#define SK_MSG_STRET(T) ((T)objc_msgSend_stret)
#else
#define SK_MSG_STRET(T) ((T)objc_msgSend)
#endif

/* Foundation's NSRange, in UTF-16 units, which CoreFoundation does not
 * define, passed and returned by value as the two words it is; and
 * NSNotFound, NSIntegerMax. */
typedef struct {
    unsigned long location, length;
} NSRange;
#define NSNotFound ((unsigned long)LONG_MAX)

/* Type encodings for class_addMethod. */
#if OBJC_BOOL_IS_BOOL
#define SK_ENC_BOOL "B"
#else
#define SK_ENC_BOOL "c"
#endif
#define SK_ENC_RANGE "{_NSRange=QQ}"
#define SK_ENC_POINT "{CGPoint=dd}"
#define SK_ENC_RECT "{CGRect={CGPoint=dd}{CGSize=dd}}"

enum {
    NSWindowStyleMaskTitled = 1 << 0,
    NSWindowStyleMaskClosable = 1 << 1,
    NSWindowStyleMaskMiniaturizable = 1 << 2,
    NSWindowStyleMaskResizable = 1 << 3,
    NSBackingStoreBuffered = 2,
};

enum {
    NSApplicationActivationPolicyRegular = 0,
    NSApplicationActivationPolicyAccessory = 1,
    NSTerminateCancel = 0,
    NSViewLayerContentsRedrawNever = 0,
};

/* NSEventType. */
enum {
    NSEventTypeLeftMouseDown = 1,
    NSEventTypeLeftMouseUp = 2,
    NSEventTypeRightMouseDown = 3,
    NSEventTypeRightMouseUp = 4,
    NSEventTypeMouseMoved = 5,
    NSEventTypeLeftMouseDragged = 6,
    NSEventTypeRightMouseDragged = 7,
    NSEventTypeMouseEntered = 8,
    NSEventTypeMouseExited = 9,
    NSEventTypeKeyDown = 10,
    NSEventTypeKeyUp = 11,
    NSEventTypeFlagsChanged = 12,
    NSEventTypeApplicationDefined = 15,
    NSEventTypeScrollWheel = 22,
    NSEventTypeOtherMouseDown = 25,
    NSEventTypeOtherMouseUp = 26,
    NSEventTypeOtherMouseDragged = 27,
};

/* NSEventModifierFlags. */
enum {
    NSEventModifierFlagCapsLock = 1 << 16,
    NSEventModifierFlagShift = 1 << 17,
    NSEventModifierFlagControl = 1 << 18,
    NSEventModifierFlagOption = 1 << 19,
    NSEventModifierFlagCommand = 1 << 20,
    NSEventModifierFlagNumericPad = 1 << 21,
    NSEventModifierFlagFunction = 1 << 23,
};

#define NSEventMaskAny (~0ull)

/* NSDragOperation. */
enum {
    NSDragOperationNone = 0,
    NSDragOperationCopy = 1,
};

/* NSTrackingAreaOptions. */
enum {
    NSTrackingMouseEnteredAndExited = 0x01,
    NSTrackingMouseMoved = 0x02,
    NSTrackingCursorUpdate = 0x04,
    NSTrackingActiveAlways = 0x80,
    NSTrackingInVisibleRect = 0x200,
    NSTrackingEnabledDuringMouseDrag = 0x400,
};

extern id NSApp;
extern id const NSPasteboardTypeString;
extern id const NSPasteboardTypeFileURL;
extern id const NSPasteboardURLReadingFileURLsOnlyKey;
extern id const kCAGravityTopLeft;
extern id const kCAGravityBottomLeft;

/* libobjc's autorelease pools, the functions @autoreleasepool becomes. */
void *objc_autoreleasePoolPush(void);
void objc_autoreleasePoolPop(void *pool);

static inline SEL sk_sel(const char *name)
{
    return sel_registerName(name);
}

static inline id sk_cls(const char *name)
{
    return (id)objc_getClass(name);
}

/* The message shapes most calls take. */
static inline id sk_msg(id o, const char *sel)
{
    return SK_MSG(id (*)(id, SEL))(o, sk_sel(sel));
}

static inline id sk_msg_id(id o, const char *sel, id a)
{
    return SK_MSG(id (*)(id, SEL, id))(o, sk_sel(sel), a);
}

static inline void sk_msg_void(id o, const char *sel)
{
    SK_MSG(void (*)(id, SEL))(o, sk_sel(sel));
}

static inline void sk_msg_void_id(id o, const char *sel, id a)
{
    SK_MSG(void (*)(id, SEL, id))(o, sk_sel(sel), a);
}

static inline void sk_msg_void_bool(id o, const char *sel, BOOL a)
{
    SK_MSG(void (*)(id, SEL, BOOL))(o, sk_sel(sel), a);
}

static inline void sk_msg_void_double(id o, const char *sel, double a)
{
    SK_MSG(void (*)(id, SEL, double))(o, sk_sel(sel), a);
}

static inline void sk_msg_void_long(id o, const char *sel, long a)
{
    SK_MSG(void (*)(id, SEL, long))(o, sk_sel(sel), a);
}

static inline void sk_msg_void_id_long(id o, const char *sel, id a, long b)
{
    SK_MSG(void (*)(id, SEL, id, long))(o, sk_sel(sel), a, b);
}

static inline BOOL sk_msg_bool(id o, const char *sel)
{
    return SK_MSG(BOOL (*)(id, SEL))(o, sk_sel(sel));
}

static inline BOOL sk_responds(id o, const char *sel)
{
    return SK_MSG(BOOL (*)(id, SEL, SEL))(o, sk_sel("respondsToSelector:"), sk_sel(sel));
}

static inline double sk_msg_double(id o, const char *sel)
{
    return SK_MSG(double (*)(id, SEL))(o, sk_sel(sel));
}

static inline long sk_msg_long(id o, const char *sel)
{
    return SK_MSG(long (*)(id, SEL))(o, sk_sel(sel));
}

static inline unsigned long sk_msg_ulong(id o, const char *sel)
{
    return SK_MSG(unsigned long (*)(id, SEL))(o, sk_sel(sel));
}

static inline CGRect sk_msg_rect(id o, const char *sel)
{
    return SK_MSG_STRET(CGRect (*)(id, SEL))(o, sk_sel(sel));
}

static inline CGPoint sk_msg_point(id o, const char *sel)
{
    return SK_MSG(CGPoint (*)(id, SEL))(o, sk_sel(sel));
}

static inline id sk_retain(id o)
{
    return sk_msg(o, "retain");
}

static inline void sk_release(id o)
{
    if (o)
        sk_msg_void(o, "release");
}

static inline id sk_autorelease(id o)
{
    return sk_msg(o, "autorelease");
}

/* [[cls alloc] init], owned by the caller. */
static inline id sk_new(const char *cls)
{
    return sk_msg(sk_msg(sk_cls(cls), "alloc"), "init");
}

/* An NSString of utf8, autoreleased; invalid UTF-8 gives "". */
static inline id sk_nsstring(const char *utf8)
{
    id s = sk_msg(sk_cls("NSString"), "alloc");
    s = SK_MSG(id (*)(id, SEL, const char *))(s, sk_sel("initWithUTF8String:"), utf8);
    return s ? sk_autorelease(s) : sk_msg(sk_cls("NSString"), "string");
}

/* The string's UTF-8, owned by it (or by the pool); NULL for nil. */
static inline const char *sk_utf8(id s)
{
    return s ? SK_MSG(const char *(*)(id, SEL))(s, sk_sel("UTF8String")) : NULL;
}

/* Adds an instance variable holding a C pointer to cls, which is not
 * yet registered. */
static inline void sk_add_ptr_ivar(Class cls, const char *name)
{
    if (!class_addIvar(cls, name, sizeof(void *), sizeof(void *) == 8 ? 3 : 2, "^v"))
        abort();
}

#endif
