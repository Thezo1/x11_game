// NOTE(Zourt): Using xlib for events because xcb events has a bug or it's not working how I expect, I don't know

#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<stdint.h>
#include<stdbool.h>

#include<sys/mman.h>

#include<X11/Xlib.h>
#include<X11/Xlib-xcb.h>
#include<X11/keysym.h>

#include<xcb/xcb.h>
#include<xcb/xcb_icccm.h>
#include<xcb/xcb_image.h>
#include<xcb/xcb_keysyms.h>


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

typedef struct X11Context
{
    Display *display;
    xcb_connection_t *connection;

    const xcb_setup_t *setup;

    xcb_atom_t wm_protocols;
    xcb_atom_t wm_delete_window;

    u32 window;
}X11Context;

typedef struct X11OffScreenBuffer
{
    xcb_image_t *image;
    xcb_pixmap_t pixmap;
    xcb_gcontext_t graphics_context;

    u32 width;
    u32 height;
    u32 pitch;
    void *memory;

}X11OffScreenBuffer;

global_variable X11OffScreenBuffer global_back_buffer;

internal void x11_update_window(X11Context *context, X11OffScreenBuffer buffer)
{
     xcb_copy_area(context->connection, 
             buffer.pixmap, 
             context->window, buffer.graphics_context, 
             0, 0, 0, 0,
             buffer.width, buffer.height);
}

internal void x11_resize_back_buffer(X11Context *context, X11OffScreenBuffer *buffer, u32 width, u32 height)
{
    int bytes_per_pixel = 4;
    if(buffer->memory)
    {
        // NOTE:(zourt) for some reason this (munmap) dumps when window is resized
        // NOTE:(zourt) if application core dumps, this may be a culprit
        munmap(buffer->memory, (buffer->width * buffer->height) * bytes_per_pixel);
    }

    if(buffer->pixmap)
    {
        xcb_free_pixmap(context->connection, buffer->pixmap);
    }

    if(buffer->graphics_context)
    {
        xcb_free_gc(context->connection, buffer->graphics_context);
    }

    buffer->width = width;
    buffer->height = height;

    // NOTE(Zourt): 24 is the pixmap depth, i.e rgb each with 8 bits
    buffer->pixmap = xcb_generate_id(context->connection);
    xcb_create_pixmap(context->connection, 24, buffer->pixmap, context->window, width, height);

    buffer->graphics_context = xcb_generate_id(context->connection);
    xcb_create_gc(context->connection, buffer->graphics_context, buffer->pixmap, 0, 0);


    u8 pad = 32;
    u8 depth = 24;
    u8 bbp = 32;

    buffer->pitch = width * bytes_per_pixel;
    usize image_size = buffer->pitch * height;
    void *image_data = mmap(0,
            (width * height) * bytes_per_pixel,
            PROT_READ | PROT_WRITE,
            MAP_ANONYMOUS | MAP_PRIVATE,
            -1,
            0);

    buffer->memory = image_data;

    buffer->image = xcb_image_create(width, height, XCB_IMAGE_FORMAT_Z_PIXMAP, pad, depth, bbp, 0, 
            (xcb_image_order_t)context->setup->image_byte_order, XCB_IMAGE_ORDER_LSB_FIRST, buffer->memory, image_size, (u8 *)buffer->memory);

}

internal bool x11_handle_events(X11Context *context, XEvent *event)
{
    bool should_quit = 0;
    switch(event->type)
    {
        case(KeyPress):
        case(KeyRelease):
        {
            bool is_down = (event->type == KeyPress);
            XKeyEvent *e = (XKeyEvent *)event;
            KeySym keysym = XLookupKeysym(e, 0);
            // xcb_key_press_event_t *e = (xcb_key_press_event_t *)event;
            // xcb_keysym_t keysym = xcb_key_press_lookup_keysym(context->ksm, e, 0);

            if(keysym == XK_F4 || (keysym == XK_F4 && (e->state & XCB_MOD_MASK_1)))
            {
                should_quit = 1;
            }
        }break;

        
#if 1
        case(Expose):
        {
            x11_update_window(context, global_back_buffer);
        }break;

        case(ResizeRequest):
        {
            XResizeRequestEvent *resize = (XResizeRequestEvent *)event;
            u32 width = resize->width;
            u32 height = resize->height;
            printf("(width: %i, height: %i)\n", width, height);

            x11_resize_back_buffer(context, &global_back_buffer,  width, height);
        }break;
#endif
    }
    return should_quit;
}

internal void load_atoms(X11Context *context)
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
    X11Context context = {};
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
                    | XCB_EVENT_MASK_EXPOSURE
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
                while(XPending(context.display))
                {
                    XEvent event;
                    XNextEvent(context.display, &event);
                    if(x11_handle_events(&context, &event))
                    {
                        global_running = 0;
                    }
                }
            }
        }
        else
        {
            fprintf(stderr, "Unable to Connect to the X11 server\n");
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
