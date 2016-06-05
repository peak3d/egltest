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
#include <sys/mman.h>

#include <libdrm/drm_fourcc.h>

#define WIDTH 1920
#define HEIGHT 1080


typedef int ion_handle;

struct ion_handle_data
{
  ion_handle handle;
};

struct ion_fd_data
{
  ion_handle handle;
  int fd;
};

struct ion_allocation_data
{
  size_t len;
  size_t align;
  unsigned int heap_id_mask;
  unsigned int flags;
  ion_handle handle;
};

#define ION_IOC_MAGIC 'I'

#define ION_IOC_ALLOC _IOWR(ION_IOC_MAGIC, 0, struct ion_allocation_data)
#define ION_IOC_FREE  _IOWR(ION_IOC_MAGIC, 1, struct ion_handle_data)
#define ION_IOC_SHARE _IOWR(ION_IOC_MAGIC, 4, struct ion_fd_data)

enum ion_heap_type
{
  ION_HEAP_TYPE_SYSTEM,
  ION_HEAP_TYPE_SYSTEM_CONTIG,
  ION_HEAP_TYPE_CARVEOUT,
  ION_HEAP_TYPE_CHUNK,
  ION_HEAP_TYPE_CUSTOM,
  ION_NUM_HEAPS = 16
};

#define ION_HEAP_SYSTEM_MASK        (1 << ION_HEAP_TYPE_SYSTEM)
#define ION_HEAP_SYSTEM_CONTIG_MASK (1 << ION_HEAP_TYPE_SYSTEM_CONTIG)
#define ION_HEAP_CARVEOUT_MASK      (1 << ION_HEAP_TYPE_CARVEOUT)


int main(int argc, char *argv[])
{
    int v4l2_dev = -1, ion_dev = -1, dma_fd = -1;

    size_t len = 1920*1080*4;

    if (!(ion_dev = open("/dev/ion", O_RDWR)))
    {
        printf("Cannot open ION memory management device /dev/ion: %s\n", strerror(errno));
        return -1;
    }

    ion_allocation_data data =
    {
      .len = len,
      .align = 0,
      .heap_id_mask = ION_HEAP_CARVEOUT_MASK,
      .flags = 0
    };

    if (ioctl(ion_dev, ION_IOC_ALLOC, &data) < 0)
    {
      printf("ION_IOC_ALLOC failed (len = %d): %s\n", data.len, strerror(errno));
      return -1;
    }

    ion_fd_data dma_data =
    {
        .handle = data.handle,
        .fd = -1
    };

    if(ioctl(ion_dev, ION_IOC_SHARE, &dma_data) < 0)
    {
      printf("ION_IOC_SHARE failed: %s\n", strerror(errno));
      return -1;
    }
    dma_fd = dma_data.fd;
    void* dma_ptr = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, dma_fd, 0);
    printf("Successfully retrieved DMA (fd:%d, ptr:%X)\n", dma_fd, (unsigned int)dma_ptr);

    /*
    if (!(v4l2_dev = open("/dev/video13", O_RDWR | O_NONBLOCK)))
    {
        printf("Cannot open AML video device /dev/video10: %s", strerror(errno));
        return -1;
    }

    v4l2_format fmt;
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix_mp.width = WIDTH;
    fmt.fmt.pix_mp.height = HEIGHT;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;

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
    */


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
        memset(&dma_buf, 0, sizeof(dma_buf));
        dma_buf.fd = dma_fd;
        dma_buf.size = len;
        dma_buf.ptr = dma_ptr;

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
        pixmap.flags = (fbdev_pixmap_flags)(FBDEV_PIXMAP_SUPPORTS_UMP);
        pixmap.data = (short unsigned int*)(&dma_buf);

//#define DMAIMP
#ifdef SURF
        EGLSurface surface = eglCreatePixmapSurface(display, config, (EGLNativePixmapType)&pixmap, NULL); 
        if (surface == EGL_NO_SURFACE) {
            printf("failed to create DMA surface %d\n",eglGetError());
            return -1;
        }
#elif defined DMAIMP
        static EGLenum EGL_LINUX_DMA_BUF_EXT  = 0x3270;
        static EGLint EGL_LINUX_DRM_FOURCC_EXT = 0x3271;
        static EGLint EGL_DMA_BUF_PLANE0_FD_EXT = 0x3272;
        static EGLint EGL_DMA_BUF_PLANE0_OFFSET_EXT = 0x3273;
        static EGLint EGL_DMA_BUF_PLANE0_PITCH_EXT = 0x3274;

        EGLint img_attrs[] = {
            EGL_WIDTH,
            WIDTH,
            EGL_HEIGHT,
            HEIGHT,
            EGL_LINUX_DRM_FOURCC_EXT,
            DRM_FORMAT_XRGB8888,
            EGL_DMA_BUF_PLANE0_FD_EXT,
            dma_fd,
            EGL_DMA_BUF_PLANE0_OFFSET_EXT,
            0,
            EGL_DMA_BUF_PLANE0_PITCH_EXT,
            0,
            EGL_NONE
        };

        EGLImageKHR image = eglCreateImageKHR(display, context, EGL_LINUX_DMA_BUF_EXT, 0, img_attrs);

        if (image == EGL_NO_IMAGE_KHR)
        {
                printf("failed to create DMA pixmap %X\n",eglGetError());
                return -1;
        }
#else
        EGLImageKHR image  = eglCreateImageKHR(display, EGL_NO_CONTEXT, EGL_NATIVE_PIXMAP_KHR, (EGLNativePixmapType)&pixmap, NULL);

        if (image == EGL_NO_IMAGE_KHR) {
                printf("failed to create DMA pixmap %X\n",eglGetError());
                return -1;
        }
#endif
    }
    return 0;
}

