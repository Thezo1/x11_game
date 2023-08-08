// NOTE: alsa does not allow mulltiple application to access the audio card (SDL be looking sexy right now)
// TODO: Implement Logging
// TODO: Manually map gamepad Key bindings on a file

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<stdint.h>
#include<stdbool.h>

#include<sys/mman.h>
#include<sys/shm.h>

#include<X11/Xlib.h>
#include<X11/Xutil.h>
#include<X11/Xatom.h>
#include<X11/Xlib-xcb.h>
#include<X11/keysym.h>
#include<X11/extensions/XShm.h>

#include<libudev.h>
#include<linux/input.h>

#include<unistd.h>
#include<fcntl.h>
#include<dirent.h>

#include<GL/glx.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef size_t usize;
typedef float f32;
typedef double f64;
typedef bool b32;

#define global_variable static
#define local_persist static
#define internal static

#define X11_MAX_CONTROLLER 4
#define DEV_INPUT_EVENT "/dev/input/event"


global_variable b32 global_running;

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

X11Controller *controller_handles[X11_MAX_CONTROLLER];
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
    u8 *src_row = (u8 *)buffer.memory;
    u8 *dst_row = (u8 *)buffer.image->data;
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

    XShmPutImage(context->display, context->window, buffer.graphics_context, buffer.image, 
             0, 0, 0, 0, 
             buffer.width, buffer.height, 0);
    XFlush(context->display);
#else
    glViewport(0, 0, buffer.width, buffer.height);

    GLuint texture_handle = 0;
    static bool init = 0;
    if(!init)
    {
        glGenTextures(1, &texture_handle);
        init = 1;
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0); 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0); 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, 0); 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, 0); 

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP); 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP); 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP);

    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    glBindTexture(GL_TEXTURE_2D, texture_handle);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 
                 buffer.width, buffer.height, 0, 
                 GL_BGRA_EXT, GL_UNSIGNED_BYTE, buffer.memory);

    glEnable(GL_TEXTURE_2D);

    glClearColor(0.18f, 0.0f, 0.18f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    f32 P = 1.0f;
    glBegin(GL_TRIANGLES);
    // Lower Triangle
        glTexCoord2f(0.0f, 0.0f);
        glVertex2f(-P, -P);

        glTexCoord2f(1.0f, 0.0f);
        glVertex2f(P, -P);

        glTexCoord2f(1.0f, 1.0f);
        glVertex2f(P, P);

    // Upper Triangle
        glTexCoord2f(0.0f, 0.0f);
        glVertex2f(-P, -P);

        glTexCoord2f(1.0f, 1.0f);
        glVertex2f(P, P);

        glTexCoord2f(0.0f, 1.0f);
        glVertex2f(-P, P);
    glEnd();

    glXSwapBuffers(context->display, context->window);
#endif
}

internal void x11_resize_back_buffer(X11Context *context, X11OffScreenBuffer *buffer, u32 width, u32 height)
{
    int bytes_per_pixel = 4;
    int bits_per_pixel = 32;
    if(buffer->memory)
    {
        munmap(buffer->memory, (buffer->width * buffer->height) * bytes_per_pixel);
    }

    if(buffer->image)
    {
        XShmDetach (context->display, &buffer->shminfo);
        XDestroyImage (buffer->image);
        shmdt(buffer->shminfo.shmaddr);
        shmctl(buffer->shminfo.shmid, IPC_RMID, 0);
    }
    buffer->width = width;
    buffer->height = height;

    buffer->pitch = width * bytes_per_pixel;
    usize image_size = buffer->pitch * height;
    buffer->memory = mmap(0,
            (width * height) * bytes_per_pixel,
            PROT_READ | PROT_WRITE,
            MAP_ANONYMOUS | MAP_PRIVATE,
            -1,
            0);
    buffer->image = XShmCreateImage(context->display, context->visual_info->visual, context->visual_info->depth, 
                                            ZPixmap, NULL, &buffer->shminfo, width, height);

    buffer->shminfo.shmid = shmget(IPC_PRIVATE, (buffer->width * buffer->height * bytes_per_pixel), IPC_CREAT | 0777);
    buffer->shminfo.shmaddr = buffer->image->data = shmat(buffer->shminfo.shmid, 0, 0);
    buffer->shminfo.readOnly = 0;
    XShmAttach(context->display, &buffer->shminfo);
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
            // printf("(width: %i, height: %i)\n", width, height);
            x11_resize_back_buffer(context, &global_back_buffer,  width, height);
        }break;

        case(ClientMessage):
        {
            if(event->xclient.data.l[0] == context->wm_delete_window)
            {
                should_quit = 1;
            }
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

internal int string_length(char *string)
{
    int count = 0;
    while(*string++)
    {
        count ++;
    }
    return(count);
}

#if 0
internal void x11_init_audio()
{
    char *device = (char *)"default";

    int err;
    snd_pcm_sframes_t frames;

    snd_output_t *alsa_log;
    snd_pcm_t *handle;
    snd_pcm_hw_params_t *params;

    err = snd_output_stdio_attach(&alsa_log, stderr, 0);
    if((err = snd_pcm_open(&handle, device, SND_PCM_STREAM_PLAYBACK, 0)) >= 0)
    {
        snd_pcm_hw_params_alloca(&params);
        snd_pcm_hw_params_any(handle, params);

        err = snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
        if(!err)
        {
            printf("set access: SND_PCM_ACCESS_RW_INTERLEAVED\n");
        }

        err = snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE);
        if(!err)
        {
            printf("set format: SND_PCM_FORMAT_S16_LE\n");
        }

        err = snd_pcm_hw_params_set_rate(handle, params, 46000,0);
        if(!err)
        {
            printf("set rate: %d\n", 46000);
        }

        err = snd_pcm_hw_params_set_channels(handle, params, 2);
        if(!err)
        {
            printf("set channels: %d\n", 2);
        }

        err = snd_pcm_hw_params_set_buffer_size(handle, params, buffer_size);
        if(!err)
        {
            printf("set buffer size: %d\n", buffer_size);
        }

        err = snd_pcm_hw_params(handle, params);
        if(!err)
        {
            printf("SET HWPARAMS and now in SND_PCM_STATE_PREPARED\n\n");
        }
        snd_pcm_dump(handle, alsa_log);
    }

    else
    {
        printf("Sound device not opened\n");
    }
}
#endif

void x11_init_opengl(X11Context *context)
{
    bool direct = 1;
    GLXContext gl_context = glXCreateContext(context->display, context->visual_info, NULL, direct);
    glXMakeCurrent(context->display, context->window, gl_context);

    printf("GL Vendor: %s\n", glGetString(GL_VENDOR));
    printf("GL Renderer: %s\n", glGetString(GL_RENDERER));
    printf("GL Shading Language: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
    printf("GL Version: %s\n", glGetString(GL_VERSION));
}

int CompareStrings(const char *str1, const char *str2) {
    while (*str1 && *str2 && *str1 == *str2) {
        str1++;
        str2++;
    }

    // Check if both strings have reached the null terminator at the same time (equal strings)
    if (*str1 == *str2)
        return 0;

    // Return a positive value if str1 is lexicographically greater than str2
    // Return a negative value if str1 is lexicographically smaller than str2
    return (*str1 > *str2) ? 1 : -1;
}

int compareStringsLimited(const char *str1, const char *str2, int limit) {
    int i = 0;
    while (i < limit && str1[i] && str2[i] && str1[i] == str2[i]) {
        i++;
    }

    // Check if both strings have reached the limit or if they have the same character at the limit
    if (i == limit || str1[i] == str2[i])
        return 0;

    // Return a positive value if str1 is lexicographically greater than str2
    // Return a negative value if str1 is lexicographically smaller than str2
    return (str1[i] > str2[i]) ? 1 : -1;
}

internal u32 x11_init_controllers()
{
    struct udev *udev = NULL;
    struct udev_device *dev = NULL;
    struct udev_enumerate *enumerate = NULL;
    struct udev_list_entry *devices = NULL;
    struct udev_list_entry *item = NULL;

    bool result = 0;
    udev = udev_new();
    if (!udev)
    {
        fprintf(stderr, "Cannot create udev context.\n");
        return(0);
    }
    enumerate = udev_enumerate_new(udev);
    if (!enumerate) {
        fprintf(stderr, "Cannot create enumerate context.\n");
        return(0);
    }

    udev_enumerate_add_match_subsystem(enumerate, "input");
    udev_enumerate_add_match_property(enumerate, "ID_INPUT_JOYSTICK", "1");
    udev_enumerate_scan_devices(enumerate);
    devices = udev_enumerate_get_list_entry(enumerate);
    if (!devices)
    {
        // TODO: logging disconnected gamepad
        fprintf(stderr, "Failed to get device list.\n");
        return(0);
    }

    u32 device_index = 0;
    for(item = devices;
        item;
        item = udev_list_entry_get_next(item))
    {
        if(device_index < X11_MAX_CONTROLLER)
        {
            const char *name;

            name = udev_list_entry_get_name(item);
            dev = udev_device_new_from_syspath(udev, name);

            const char *devpath = udev_device_get_devnode(dev);
            if(devpath && (compareStringsLimited(devpath, DEV_INPUT_EVENT, 16) == 0))
            {
                // TODO: Use X11_Calloc() instead
                controller_handles[device_index] = (X11Controller*)calloc(1, sizeof(X11Controller));

                strcpy(controller_handles[device_index]->path, devpath);
                controller_handles[device_index]->fd = open(devpath, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
                if(controller_handles[device_index]->fd < 0)
                {
                    return(0);
                }
                ++device_index;
                result += 1;
            }
        }
        else
        {
            // TODO: Logging Max Controllers exceeded
            return(0);
        }
    }

    udev_device_unref(dev);
    udev_enumerate_unref(enumerate);
    udev_unref(udev);
    return(result);
}

int main(int argc, char **argv)
{
    X11Context context = {};
    bool controller = x11_init_controllers();

    context.display = XOpenDisplay(NULL);
    int *screen_number;

    if(context.display)
    {


#if 0
        int major;
        int minor;
        if(glXQueryVersion(context.display, &major, &minor))
        {
            fprintf(stdout, "GLX version: %i.%i\n", major, minor);
        }
        else
        {
            fprintf(stderr, "GLX not found\n");
            exit(1);
        }

#else
        int major_version;
        int minor_version;
        int pixmaps;
        if(XShmQueryVersion(context.display, &major_version, &minor_version, &pixmaps))
        {
            fprintf(stdout, "MIT-SHM version: %i.%i, pixmaps: %i\n", major_version, minor_version, pixmaps);
        }
        else
        {
            fprintf(stderr, "MIT-SHM extension not found");
            exit(1);
        }

#endif

        GLint gl_attribs[] = 
        {
            GLX_RGBA,
            GLX_DOUBLEBUFFER,
            GLX_DEPTH_SIZE, 24,
            GLX_STENCIL_SIZE, 8,
            GLX_RED_SIZE, 8,
            GLX_GREEN_SIZE, 8,
            GLX_BLUE_SIZE, 8,
            GLX_SAMPLE_BUFFERS, 0,
            GLX_SAMPLES, 0,
            None
        };

        int root = DefaultRootWindow(context.display);
        context.default_screen = DefaultScreen(context.display);

        context.visual_info = glXChooseVisual(context.display, context.default_screen, gl_attribs);
        if(!context.visual_info)
        {
            fprintf(stderr, "Could not choose a visual");
        }

        XSetWindowAttributes window_attribute;
        window_attribute.backing_pixel = 0;
        window_attribute.colormap = XCreateColormap(context.display, root, context.visual_info->visual, AllocNone);

        window_attribute.event_mask = 
              StructureNotifyMask 
            | KeyPressMask 
            | KeyReleaseMask 
            | ResizeRedirectMask
            | ExposureMask;
        u64 attribute_mask = CWBackPixel | CWColormap | CWEventMask;

#define SCREEN_WIDTH 960
#define SCREEN_HEIGHT 540
// define SCREEN_WIDTH 1920
// define SCREEN_HEIGHT 1080

        context.window = XCreateWindow(context.display, root, 0, 0,
                SCREEN_WIDTH, SCREEN_HEIGHT, 0,
                context.visual_info->depth, InputOutput,
                context.visual_info->visual, attribute_mask, &window_attribute);
        if(context.window)
        {
            XStoreName(context.display, context.window, "GAME");
            // x11_init_opengl(&context);

            context.wm_delete_window = XInternAtom(context.display, "WM_DELETE_WINDOW", 0);
            XSetWMProtocols(context.display, context.window, &context.wm_delete_window, 1);

            XClearWindow(context.display, context.window);
            XMapWindow(context.display, context.window);
            global_back_buffer.graphics_context = XDefaultGC(context.display, context.default_screen);

            XFlush(context.display);

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
                if(controller)
                {
                    struct input_event events[32];
                    for(u32 controller_index = 0;
                            controller_index < controller;
                            ++controller_index)
                    {
                        struct input_absinfo abs_x;
                        u32 fd = controller_handles[controller_index]->fd;
                        // ioctl(fd, EVIOCGABS(ABS_X), &abs_x);
                        // printf("%i\n", abs_x.minimum);
                        // printf("%i\n", abs_x.maximum);

                        unsigned long evbits[EV_MAX/32 + 1] = {0};
                        ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits);

                        usize bytes = read(fd, events, sizeof(events));
                        if(bytes != -1)
                        {
                            u32 new_event_count = bytes/sizeof(struct input_event);
                            for(int ev_idx = 0;
                                    ev_idx < new_event_count;
                                    ++ev_idx)
                            {
                                struct input_event ev = events[ev_idx];
                                switch(ev.type)
                                {
                                    case EV_KEY:
                                    {
                                        if(ev.value == 1)
                                        {
                                            printf("Key Pressed: %i\n", ev.code);
                                        }
                                    }
                                }
                                
                            }
                        }
                    }
                }

                draw_to_buffer(&global_back_buffer, blue_offset, red_offset);
                x11_update_window(&context, global_back_buffer);
                blue_offset += 1;
            }
        }
        else
        {
            fprintf(stderr, "Unable to create window\n");
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
