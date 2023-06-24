// NOTE(Zourt): Using xlib for events because xcb events has a bug or it's not working how I expect, I don't know

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<stdint.h>
#include<stdbool.h>

#include<sys/mman.h>
#include<sys/shm.h>

#include<X11/Xlib.h>
#include<X11/Xlib-xcb.h>
#include<X11/keysym.h>

#include<xcb/xcb.h>
#include<xcb/xcb_icccm.h>
#include<xcb/xcb_image.h>
#include<xcb/shm.h>

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
    u8 *shmaddr;
    xcb_pixmap_t pixmap;
    xcb_gcontext_t graphics_context;

    u32 width;
    u32 height;
    u32 pitch;
    void *memory;

}X11OffScreenBuffer;

global_variable X11OffScreenBuffer global_back_buffer;

internal void draw_to_buffer(X11OffScreenBuffer *buffer, u32 blue_offset, u32 red_offset)
{
    u8 *row = (u8 *)buffer->memory;
    for(u32 y = 0;
            y < buffer->height;
            ++y)
    {
        u32 *pixel = (u32 *)row;
        for(u32 x = 0;
                x < buffer->width;
                ++x)
        {
            u8 blue = x + blue_offset;
            u8 red = y + red_offset;
            *pixel++ = ((red << 16) | blue);
        }
        row += buffer->pitch;
    }

}

internal void x11_update_window(X11Context *context, X11OffScreenBuffer buffer)
{

#if 1
    // NOTE(Zourt): copy data from custom buffer to the shm

    u8 *src_row = (u8 *)buffer.memory;
    u8 *dst_row = (u8 *)buffer.shmaddr;
    for(u32 y = 0;
            y < buffer.height;
            ++y)
    {
        u32 *dest_pixel = (u32 *)dst_row;
        u32 *src_pixel = (u32 *)src_row;
        for(u32 x = 0;
                x < buffer.width;
                ++x)
        {
            u8 blue = x;
            u8 red = y;
            *dest_pixel++ = *src_pixel++;
        }
        dst_row += buffer.pitch;
        src_row += buffer.pitch;
    }

    xcb_copy_area(context->connection, 
            buffer.pixmap, 
            context->window, buffer.graphics_context, 
            0, 0, 0, 0,
            buffer.width, buffer.height);
    xcb_flush(context->connection);
#endif
}

internal void x11_resize_back_buffer(X11Context *context, X11OffScreenBuffer *buffer, u32 width, u32 height)
{
    int bytes_per_pixel = 4;

    if(buffer->memory)
    {
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
    u8 depth = 24;

    buffer->pixmap = xcb_generate_id(context->connection);
    xcb_create_pixmap(context->connection, depth, buffer->pixmap, context->window, width, height);

    buffer->graphics_context = xcb_generate_id(context->connection);
    xcb_create_gc(context->connection, buffer->graphics_context, buffer->pixmap, 0, 0);


    u8 pad = 32;
    u8 bbp = 32;
    buffer->pitch = width * bytes_per_pixel;
    usize image_size = buffer->pitch * height;
    buffer->memory = mmap(0,
            (width * height) * bytes_per_pixel,
            PROT_READ | PROT_WRITE,
            MAP_ANONYMOUS | MAP_PRIVATE,
            -1,
            0);

    // buffer->image = xcb_image_create(width, height, XCB_IMAGE_FORMAT_Z_PIXMAP, pad, depth, bbp, 0, 
    //         (xcb_image_order_t)context->setup->image_byte_order, XCB_IMAGE_ORDER_LSB_FIRST, buffer->memory, image_size, (u8 *)buffer->memory);

#if 1
    xcb_shm_segment_info_t shm_info = {0};
    shm_info.shmid = shmget(IPC_PRIVATE, (buffer->width * buffer->height * bytes_per_pixel), IPC_CREAT | 0666);
    shm_info.shmaddr = shmat(shm_info.shmid, 0, 0);
    shm_info.shmseg = xcb_generate_id(context->connection);

    xcb_shm_attach(context->connection, shm_info.shmseg, shm_info.shmid, 0);
    shmctl(shm_info.shmid, IPC_RMID, 0);

    buffer->pixmap = xcb_generate_id(context->connection);
    xcb_shm_create_pixmap(context->connection, buffer->pixmap, context->window, width, height, depth, shm_info.shmseg, 0);
    buffer->shmaddr = shm_info.shmaddr;

#endif
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

            if(keysym == XK_F4 || (keysym == XK_F4 && (e->state & XCB_MOD_MASK_1)))
            {
                should_quit = 1;
            }
        }break;

        
        // NOTE(Zourt): does this need to be here
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

#if 0
        case(ConfigureNotify):
        {

            XWindowAttributes wa = {0};
            XGetWindowAttributes(context->display, context->window, &wa);
            // printf("(original width: %i, original height: %i)\n", wa.width, wa.height);

            XConfigureEvent *e = (XConfigureEvent *)event;
            if(e->width != wa.width || e->height != wa.height)
            {
                u32 width = e->width;
                u32 height = e->height;
                // printf("(width: %i, height: %i)\n", width, height);

                x11_resize_back_buffer(context, &global_back_buffer,  width, height);
            }

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
                screen->black_pixel, 
                0
                    | XCB_EVENT_MASK_POINTER_MOTION
                    | XCB_EVENT_MASK_KEY_PRESS
                    | XCB_EVENT_MASK_KEY_RELEASE
                    | XCB_EVENT_MASK_BUTTON_PRESS
                    | XCB_EVENT_MASK_BUTTON_RELEASE
                    | XCB_EVENT_MASK_RESIZE_REDIRECT
                    | XCB_EVENT_MASK_EXPOSURE
            };

            #define SCREEN_WIDTH 960
            #define SCREEN_HEIGHT 540
            // #define SCREEN_WIDTH 1920
            // #define SCREEN_HEIGHT 1080

            xcb_create_window(context.connection, XCB_COPY_FROM_PARENT, 
                    context.window, screen->root, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 
                    XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, mask, values);

            uint32_t value[] = { XCB_STACK_MODE_ABOVE };
            xcb_configure_window(context.connection, context.window, XCB_CONFIG_WINDOW_STACK_MODE, value);


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

            x11_resize_back_buffer(&context, &global_back_buffer,  SCREEN_WIDTH, SCREEN_HEIGHT);
            u32 blue_offset = 0;
            u32 red_offset = 0;
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

                draw_to_buffer(&global_back_buffer, blue_offset, red_offset);
                x11_update_window(&context, global_back_buffer);
                blue_offset += 1;

                if((blue_offset%2) == 0)
                {
                    red_offset += 1;
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
