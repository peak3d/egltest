#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

#define EGL_EGLEXT_PROTOTYPES
#define MALI_USE_DMA_BUF 1

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>

#define WIDTH 1920
#define HEIGHT 1080


int main(int argc, char *argv[])
{
    int v4l2_dev = -1, ump_sid = -1;

    size_t len = 1920*1080*3;

    if (!(v4l2_dev = open("/dev/video13", O_RDWR | O_NONBLOCK)))
    {
        printf("Cannot open AML video device /dev/video10: %s", strerror(errno));
        return -1;
    }

    v4l2_format fmt;
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix_mp.width = WIDTH;
    fmt.fmt.pix_mp.height = HEIGHT;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_RGB24;//V4L2_PIX_FMT_NV12;

    if (ioctl(v4l2_dev, VIDIOC_S_FMT, &fmt) < 0)
    {
        printf("VIDIOC_S_FMT failed: %s", strerror(errno));
        return -1;
    }

    v4l2_requestbuffers req;
    req.count = 1;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(v4l2_dev, VIDIOC_REQBUFS, &req) < 0)
    {
        printf("VIDIOC_REQBUFS failed: %s", strerror(errno));
        return -1;
    }

    struct v4l2_exportbuffer expbuf;
    memset(&expbuf, 0, sizeof(expbuf));
    expbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    expbuf.index = 0;
    if (ioctl(v4l2_dev, VIDIOC_EXPBUF, &expbuf) == -1) {
        printf("VIDIOC_EXPBUF failed: %s", strerror(errno));
        return -1;
    }

    ump_sid = expbuf.fd;

    /********************  EGL ***************** */
    {
        EGLDisplay display;
        EGLConfig config;
        EGLContext context;

        EGLint major, minor, num_config;

        static const EGLint egl_config_attribs[] = {
            EGL_BUFFER_SIZE,        32,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_DEPTH_SIZE,         EGL_DONT_CARE,
            EGL_STENCIL_SIZE,       EGL_DONT_CARE,
            EGL_RENDERABLE_TYPE,    EGL_OPENGL_ES2_BIT,
            EGL_SURFACE_TYPE,       EGL_WINDOW_BIT,
            EGL_NONE,
        };

        static const EGLint context_attribs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE
        };

        display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

        if (!eglInitialize(display, &major, &minor)) {
            printf("failed to initialize %d\n",eglGetError());
            return -1;
        } else
            puts(eglQueryString(display,EGL_EXTENSIONS));

        if (!eglBindAPI(EGL_OPENGL_ES_API)) {
            printf("failed to bind api EGL_OPENGL_ES_API\n");
            return -1;
        }

        if(!eglChooseConfig(display, egl_config_attribs, &config, 1, &num_config))
        {
            printf("failed to choose config\n");
            return -1;
        }

        context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribs);
        if (context == NULL) {
            printf("failed to create context\n");
            return -1;
        }

        typedef EGLImageKHR (*eglCreateImageKHRfn)(EGLDisplay dpy, EGLContext ctx, EGLenum target, EGLClientBuffer buffer, const EGLint *attrib_list);
        eglCreateImageKHRfn eglCreateImageKHR = (eglCreateImageKHRfn) (eglGetProcAddress("eglCreateImageKHR"));

        struct fbdev_dma_buf dma_buf;
        memset(&dma_buf, 0, sizeof(struct fbdev_dma_buf));
        dma_buf.fd = dma_fd;
        dma_buf.size = len;
	dma_buf.ptr = 0;

        struct fbdev_pixmap pixmap;
        memset(&pixmap, 0, sizeof(struct fbdev_pixmap));
        pixmap.width = WIDTH;
        pixmap.height = HEIGHT;
        pixmap.bytes_per_pixel = 4;
        pixmap.buffer_size = 32;
        pixmap.red_size = 8;
        pixmap.green_size = 8;
        pixmap.blue_size = 8;
	pixmap.alpha_size = 8;
        pixmap.flags = FBDEV_PIXMAP_DMA_BUF;
        pixmap.data = (short unsigned int*)(&dma_buf);
	pixmap.format = 1;

#ifdef SURFACE
        EGLSurface surface = eglCreatePixmapSurface(display, config, (EGLNativePixmapType)&pixmap, NULL); 
        if (surface == EGL_NO_SURFACE) {
            printf("failed to create DMA surface %d\n",eglGetError());
            return -1;
        }
#else

        EGLImageKHR image  = eglCreateImageKHR(display, EGL_NO_CONTEXT, EGL_NATIVE_PIXMAP_KHR, (EGLNativePixmapType)&pixmap, NULL);

        if (image == EGL_NO_IMAGE_KHR) {
                printf("failed to create DMA pixmap %x\n",eglGetError());
                return -1;
        }
#endif
    }
    return 0;
}

