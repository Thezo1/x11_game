#ifndef _x11_GAME_
#define _x11_GAME_

#include<stdint.h>

#include<X11/Xlib.h>
#include<X11/Xutil.h>
#include<X11/Xatom.h>
#include<X11/Xlib-xcb.h>
#include<X11/extensions/XShm.h>
#include<X11/keysym.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef size_t usize;
typedef ssize_t ssize;
typedef float f32;
typedef double f64;
typedef bool b32;

#define global_variable static
#define local_persist static
#define internal static

#define X11_MAX_CONTROLLER 4
#define DEV_INPUT_EVENT "/dev/input/event"
#define X11_ACTION_COUNT 22

#if GAME_DEBUG
#define Assert(Expression) if(!(Expression)) {*(int *)0 = 0;}
#else
#define Assert(Expression)
#endif

/*
    X11_CONTROLLER_BUTTON_A,
    X11_CONTROLLER_BUTTON_B,
    X11_CONTROLLER_BUTTON_X,
    X11_CONTROLLER_BUTTON_Y,
    X11_CONTROLLER_BUTTON_BACK,
    X11_CONTROLLER_BUTTON_GUIDE,
    X11_CONTROLLER_BUTTON_START,
    X11_CONTROLLER_BUTTON_LEFT_STICK,
    X11_CONTROLLER_BUTTON_RIGHT_STICK,
    X11_CONTROLLER_BUTTON_LEFT_SHOULDER,
    X11_CONTROLLER_BUTTON_RIGHT_SHOULDER,
    X11_CONTROLLER_BUTTON_DPAD_UP,
    X11_CONTROLLER_BUTTON_DPAD_DOWN,
    X11_CONTROLLER_BUTTON_DPAD_LEFT,
    X11_CONTROLLER_BUTTON_DPAD_RIGHT,
    X11_CONTROLLER_BUTTON_MISC1,    
    X11_CONTROLLER_BUTTON_RIGHT_PADDLE1,  
    X11_CONTROLLER_BUTTON_LEFT_PADDLE1,  
    X11_CONTROLLER_BUTTON_RIGHT_PADDLE2,
    X11_CONTROLLER_BUTTON_LEFT_PADDLE2,
    X11_CONTROLLER_BUTTON_TOUCHPAD,
    X11_CONTROLLER_BUTTON_MAX
*/

#define BITS_PER_LONG (sizeof(unsigned long) * 8)
#define NBITS(x) ((((x)-1) / BITS_PER_LONG) + 1)

#define EVDEV_OFF(x) ((x) % BITS_PER_LONG)
#define EVDEV_LONG(x) ((x) / BITS_PER_LONG)
#define test_bit(bit, array) ((array[EVDEV_LONG(bit)] >> EVDEV_OFF(bit)) & 1)

enum X11ControllerBindings
{
    X11_LEFT_STICK_UP,
    X11_LEFT_STICK_DOWN,
    X11_LEFT_STICK_LEFT,
    X11_LEFT_STICK_RIGHT,

    X11_DPAD_UP,
    X11_DPAD_DOWN,
    X11_DPAD_LEFT,
    X11_DPAD_RIGHT,

    X11_RIGHT_STICK_UP,
    X11_RIGHT_STICK_DOWN,
    X11_RIGHT_STICK_LEFT,
    X11_RIGHT_STICK_RIGHT,

    X11_LEFT_SHOULDER,
    X11_RIGHT_SHOULDER,
    X11_LEFT_TRIGGER,
    X11_RIGHT_TRIGGER,

    X11_BUTTON_A,
    X11_BUTTON_B,
    X11_BUTTON_X,
    X11_BUTTON_Y,

    X11_START,
    X11_BACK,
};

typedef struct X11Context
{
    Display *display;
    Window window;

    int default_screen;
    XVisualInfo *visual_info;
    Atom wm_delete_window;
} X11Context;

typedef struct X11OffScreenBuffer
{
    XImage *image;
    XShmSegmentInfo shminfo;

    GC graphics_context;
    
    u32 width;
    u32 height;
    u32 pitch;
    void *memory;

}X11OffScreenBuffer;

typedef struct X11Controller
{
    char path[100];
    int fd;
}X11Controller;

typedef struct X11KeyBindings
{
    u16 type;
    u16 code;
    bool is_positive;
}X11KeyBindings;

typedef struct ControllerState
{
    b32 result;
    b32 pressed;
}ControllerState;

#endif
