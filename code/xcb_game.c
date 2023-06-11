#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<stdint.h>
#include<stdbool.h>

#include<X11/Xlib.h>
#include<X11/Xlib-xcb.h>
#include<X11/keysym.h>

#include<xcb/xcb.h>
#include<xcb/xcb_icccm.h>


typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef size_t usize;
typedef float f32;
typedef double f64;
typedef bool b32;

#define global_variable static
#define local_persist static
#define internal static

global_variable b32 global_running;

typedef struct XcbContext
{
    Display *display;
    xcb_connection_t *connection;

    const xcb_setup_t *setup;

    xcb_atom_t wm_protocols;
    xcb_atom_t wm_delete_window;

    u32 window;
}XcbContext;

internal void xcb_handle_events(XcbContext *context, XEvent *event)
{
    XNextEvent(context->display, event);

    switch(event->type)
    {
        case(ClientMessage):
        {
            XClientMessageEvent *client_message_event = (XClientMessageEvent *)&event;

            if(client_message_event->message_type == context->wm_protocols)
            {
                if(client_message_event->data.l[0] == context->wm_delete_window)
                {
                    fprintf(stdout, "Close Window\n");
                    global_running = 0;
                }
            }
        }break;

        case(KeyPress):
        case(KeyRelease):
        {
            XKeyEvent *e = (XKeyEvent *)&event;
            b32 is_down = (event->type == KeyPress);
            KeySym keysym = XLookupKeysym(e, 0);

            u32 modifiers = event->xkey.state;
            if(keysym == XK_F4 || (keysym == XK_F4 && (modifiers & Mod1Mask)))
            {
                global_running = 0;
            }
        }break;
    }
}

internal void load_atoms(XcbContext *context)
{
    xcb_intern_atom_cookie_t wm_delete_window_cookie = 
        xcb_intern_atom(context->connection, 0, strlen("WM_DELETE_WINDOW"), "WM_DELETE_WINDOW");

    xcb_intern_atom_cookie_t wm_protocol_cookie = 
        xcb_intern_atom(context->connection, 0, strlen("WM_PROTOCOLS"), "WM_PROTOCOLS");

    xcb_flush(context->connection);
    xcb_intern_atom_reply_t *wm_delete_window_cookie_reply = 
        xcb_intern_atom_reply(context->connection, wm_delete_window_cookie, 0);

    xcb_intern_atom_reply_t *wm_protocol_cookie_reply = 
        xcb_intern_atom_reply(context->connection, wm_protocol_cookie, 0);

    context->wm_protocols = wm_protocol_cookie_reply->atom;
    context->wm_delete_window = wm_delete_window_cookie_reply->atom;
}

int main(int argc, char **argv)
{
    XcbContext context = {};
    int default_screen;

    context.display = XOpenDisplay(NULL);
    int *screen_number;
    if(context.display)
    {
        context.connection = XGetXCBConnection(context.display);
        if(context.connection)
        {
            default_screen = DefaultScreen(context.display);

            load_atoms(&context);

            context.setup = xcb_get_setup(context.connection);
            xcb_screen_iterator_t screen_iterator = xcb_setup_roots_iterator(context.setup);
            xcb_screen_t *screen = screen_iterator.data;
            context.window = xcb_generate_id(context.connection);

            u32 mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
            u32 values[2] = 
            { 
                screen->black_pixel, //0x0000ffff,
                0
                    | XCB_EVENT_MASK_POINTER_MOTION
                    | XCB_EVENT_MASK_KEY_PRESS
                    | XCB_EVENT_MASK_KEY_RELEASE
                    | XCB_EVENT_MASK_BUTTON_PRESS
                    | XCB_EVENT_MASK_BUTTON_RELEASE
                    | XCB_EVENT_MASK_RESIZE_REDIRECT
                    ,
            };

            #define SCREEN_WIDTH 960
            #define SCREEN_HEIGHT 540

            xcb_create_window(context.connection, XCB_COPY_FROM_PARENT, 
                    context.window, screen->root, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 
                    XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, mask, values);


            const char *title = "Game";
            xcb_change_property(context.connection, XCB_PROP_MODE_REPLACE, 
                    context.window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, strlen(title), title);

            xcb_atom_t protocols[] = 
            {
                context.wm_delete_window,
            };

            xcb_icccm_set_wm_protocols(context.connection, context.window, context.wm_protocols, 1, protocols);

            xcb_map_window(context.connection, context.window);
            xcb_flush(context.connection);

            global_running = 1;
            while(global_running)
            {
                XEvent event;
                while(XPending(context.display))
                {
                    xcb_handle_events(&context, &event);
                }
            }
        }
        else
        {
            fprintf(stderr, "Unable to open Connect to the X11 server\n");
            exit(1);
        }
    }
    else
    {
        fprintf(stderr, "Unable to open X11 display\n");
        exit(1);
    }

    printf("Hello x11\n");
    return(0);
}
