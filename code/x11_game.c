// NOTE: This is just for learning purposes, I'd probably use SDL for a serious project or maybe get used to X11 and enjoy using it
// NOTE: alsa does not allow mulltiple application to access the audio card
// NOTE: GamePad input code is shit right now, I don't know, make it better later, this is just to get things working now, add support for haptics and all
// NOTE: I don't know what possessed me to use FALSE as 0 and TRUE as 1
/* TODO: 
 *
 * - Manually map gamepad Key bindings from a file.
 * provide a GUI for said mapping
 * - HotPlugging
 * - Haptics
 * - Maybe do our own keyboard and mouse instead of xlib's
 * - Implement Logging
 * - Vulkan?
*/

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<stdbool.h>

#include<sys/mman.h>
#include<sys/shm.h>


#include<libudev.h>
#include<linux/input.h>

#include<unistd.h>
#include<fcntl.h>
#include<dirent.h>

#include<GL/glx.h>
#include "x11_game.h"


global_variable b32 global_running;
X11Controller *controller_handles[X11_MAX_CONTROLLER];
global_variable X11OffScreenBuffer global_back_buffer;

static X11KeyBindings default_bindings[X11_ACTION_COUNT] = 
{
    [X11_LEFT_STICK_UP] = {.type = EV_ABS, .code = 1, .is_positive = 1},
    [X11_LEFT_STICK_DOWN] = {.type = EV_ABS, .code = 1, .is_positive = 1},
    [X11_LEFT_STICK_LEFT] = {.type = EV_ABS, .code = 0, .is_positive = 1},
    [X11_LEFT_STICK_RIGHT] = {.type = EV_ABS, .code = 0, .is_positive = 1},

    [X11_DPAD_UP] = {.type = EV_ABS, .code = 17, .is_positive = 0},
    [X11_DPAD_DOWN] = {.type = EV_ABS, .code = 17, .is_positive = 1},
    [X11_DPAD_LEFT] = {.type = EV_ABS, .code = 16, .is_positive = 0},
    [X11_DPAD_RIGHT] = {.type = EV_ABS, .code = 16, .is_positive = 1},

    [X11_RIGHT_STICK_UP] = {.type = EV_ABS, .code = 5, .is_positive = 1},
    [X11_RIGHT_STICK_DOWN] = {.type = EV_ABS, .code = 5, .is_positive = 1},
    [X11_RIGHT_STICK_LEFT] = {.type = EV_ABS, .code = 3, .is_positive = 1},
    [X11_RIGHT_STICK_RIGHT] = {.type = EV_ABS, .code = 3, .is_positive = 1},

    [X11_LEFT_SHOULDER] = {.type = EV_KEY, .code = 292, .is_positive = 1},
    [X11_RIGHT_SHOULDER] = {.type = EV_KEY, .code = 293, .is_positive = 1},
    [X11_LEFT_TRIGGER] = {.type = EV_KEY, .code = 294, .is_positive = 1},
    [X11_RIGHT_TRIGGER] = {.type = EV_KEY, .code = 295, .is_positive = 1},

    [X11_BUTTON_A] = {.type = EV_KEY, .code = 290, .is_positive = 1},
    [X11_BUTTON_B] = {.type = EV_KEY, .code = 289, .is_positive = 1},
    [X11_BUTTON_X] = {.type = EV_KEY, .code = 291, .is_positive = 1},
    [X11_BUTTON_Y] = {.type = EV_KEY, .code = 288, .is_positive = 1},

    [X11_START] = {.type = EV_KEY, .code = 297, .is_positive = 1},
    [X11_BACK] = {.type = EV_KEY, .code = 296, .is_positive = 1},
};


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
            // bool is_down = (event->type == KeyPress);
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

internal void x11_init_opengl(X11Context *context)
{
    bool direct = 1;
    GLXContext gl_context = glXCreateContext(context->display, context->visual_info, NULL, direct);
    glXMakeCurrent(context->display, context->window, gl_context);

    printf("GL Vendor: %s\n", glGetString(GL_VENDOR));
    printf("GL Renderer: %s\n", glGetString(GL_RENDERER));
    printf("GL Shading Language: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
    printf("GL Version: %s\n", glGetString(GL_VERSION));
}

internal inline int x11_strcmp(const char *str1, const char *str2) {
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

internal inline int x11_strcmp_to(const char *str1, const char *str2, int limit) {
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

internal inline int string_length(char *string)
{
    int count = 0;
    while(*string++)
    {
        count ++;
    }
    return(count);
}

internal i32 x11_scan_controllers(struct udev *udev)
{
    i32 result = 0;
    struct udev_device *dev = NULL;
    struct udev_enumerate *enumerate = NULL;
    struct udev_list_entry *devices = NULL;
    struct udev_list_entry *item = NULL;

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
            if(devpath && (x11_strcmp_to(devpath, DEV_INPUT_EVENT, 16) == 0))
            {
                // TODO: Use X11_Calloc() instead
                controller_handles[device_index] = (X11Controller*)calloc(1, sizeof(X11Controller));

                strcpy(controller_handles[device_index]->path, devpath);
                controller_handles[device_index]->fd = open(devpath, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
                fcntl(controller_handles[device_index]->fd, F_SETFL, O_NONBLOCK);

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

    return(result);
}

b32 x11_udev_has_event(struct udev_monitor *udev_monitor)
{
    int fd = udev_monitor_get_fd(udev_monitor);;

    struct timeval tv;

    fd_set fds;

    tv.tv_sec = 0;
    tv.tv_usec = 0;

    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    int ret = select(fd+1, &fds, NULL, NULL, &tv);

    return(ret > 0 && FD_ISSET(fd, &fds));
}

int main(int argc, char **argv)
{
    X11Context context = {};
    struct udev *udev = NULL;
    struct udev_monitor *udev_monitor = NULL;

    udev = udev_new();
    if (!udev)
    {
        fprintf(stderr, "Cannot create udev context.\n");
        return(-1);
    }
    udev_monitor = udev_monitor_new_from_netlink(udev, "udev");
    if (!udev_monitor)
    {
        fprintf(stderr, "Cannot create udev monitor.\n");
        return(-1);
    }

    u8 num_controllers = x11_scan_controllers(udev);
    udev_monitor_filter_add_match_subsystem_devtype(udev_monitor, "input", NULL);
    udev_monitor_enable_receiving(udev_monitor);

    context.display = XOpenDisplay(NULL);

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

        int root = DefaultRootWindow(context.display);
        context.default_screen = DefaultScreen(context.display);

#if 0
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

        context.visual_info = glXChooseVisual(context.display, context.default_screen, gl_attribs);
        if(!context.visual_info)
        {
            fprintf(stderr, "Could not choose a visual");
        }
#else
        int screen_bit_depth = 24;

        XVisualInfo visual_info = {};
        if(!XMatchVisualInfo(context.display, context.default_screen, screen_bit_depth, TrueColor, &visual_info))
        {
            printf("No Matching visual info found\n");
            exit(1);
        }
        else
        {
            context.visual_info = &visual_info;
        }
#endif
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
// #define SCREEN_WIDTH 1920
// #define SCREEN_HEIGHT 1080

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

            X11KeyBindings *bindings = default_bindings;
            while(global_running)
            {
                while(x11_udev_has_event(udev_monitor))
                {
                    struct udev_device *dev = NULL;
                    dev = udev_monitor_receive_device(udev_monitor);
                    if (dev)
                    {
                        const char *action = udev_device_get_action(dev);
                        if(action)
                        {
                            if(x11_strcmp(action, "add") == 0)
                            {
                                const char *devpath = udev_device_get_devnode(dev);
                                if(devpath && x11_strcmp_to(devpath, DEV_INPUT_EVENT, 16) == 0)
                                {
                                    for(int array_index = 0;
                                            array_index < X11_MAX_CONTROLLER;
                                            ++array_index)
                                    {
                                        if(!controller_handles[array_index])
                                        {

                                            controller_handles[array_index] = (X11Controller*)calloc(1, sizeof(X11Controller));
                                            strcpy(controller_handles[array_index]->path, devpath);
                                            controller_handles[array_index]->fd = open(devpath, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
                                            fcntl(controller_handles[array_index]->fd, F_SETFL, O_NONBLOCK);
                                            num_controllers += 1;
                                            printf("%s added\n", devpath);
                                            break;
                                        }
                                    }
                                }
                            }
                            else if(x11_strcmp(action, "remove") == 0)
                            {
                                const char *devpath = udev_device_get_devnode(dev);
                                if(devpath && x11_strcmp_to(devpath, DEV_INPUT_EVENT, 16) == 0)
                                {
                                    for(int array_index = 0;
                                            array_index < X11_MAX_CONTROLLER;
                                            ++array_index)
                                    {
                                        if(controller_handles[array_index])
                                        {
                                            if(x11_strcmp(devpath, controller_handles[array_index]->path) == 0)
                                            {
                                                close(controller_handles[array_index]->fd);
                                                free(controller_handles[array_index]);
                                                printf("%s removed\n", devpath);
                                                num_controllers -= 1;
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        /* free dev */
                        udev_device_unref(dev);
                    }
                    else
                    {
                        break;
                    }
                }

                while(XPending(context.display))
                {
                    XEvent event;
                    XNextEvent(context.display, &event);
                    if(x11_handle_events(&context, &event))
                    {
                        global_running = 0;
                    }
                }

                if(num_controllers)
                {
                    struct input_event events[32];
                    for(u32 controller_index = 0;
                            controller_index < num_controllers;
                            ++controller_index)
                    {
                        u32 fd = controller_handles[controller_index]->fd;

                        unsigned long keyinfo[NBITS(KEY_MAX)];
                        ssize bytes = read(fd, events, sizeof(events));

                        if(ioctl(fd, EVIOCGKEY(sizeof(keyinfo)), keyinfo) >= 0)
                        {
                            for(u32 i = 0;
                                    i < KEY_MAX;
                                    ++i)
                            {
                                const u8 value = test_bit(i, keyinfo) ? 1 : 0;
                                if(value && (i == bindings[X11_BUTTON_A].code))
                                {
                                    red_offset += 2;
                                }
                            }
                        }
                        else
                        {
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
