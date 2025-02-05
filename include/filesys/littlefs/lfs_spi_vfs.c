#include <unistd.h>
#include <fcntl.h>
#include <sys/vfs.h>
#include <sys/stat.h>
#include <errno.h>

#define DEFAULT_LFS_PINMASK ((1ULL<<61) | (1ULL<<60) | (1ULL<<59) | (1ULL<<58))

static struct littlefs_flash_config default_cfg = {
    .page_size = 256,
    .erase_size = 65536,
    .offset = 2*1024*1024,      // where to start in flash
    .used_size = 6*1024*1024,   // how much of flash to use
    .pinmask = DEFAULT_LFS_PINMASK,
};

typedef struct __using("filesys/littlefs/lfswrapper.cc") LFS_Wrapper;

static LFS_Wrapper default_LFS;

struct vfs *
_vfs_open_littlefs_flash(int do_format = 1, struct littlefs_flash_config *fcfg = 0)
{
    struct vfs *v;
    LFS_Wrapper *LFS;
    unsigned long long pinmask;

    if (!fcfg) {
        fcfg = &default_cfg;
    }
    pinmask = fcfg->pinmask;
    if (!pinmask) {
        fcfg->pinmask = pinmask = DEFAULT_LFS_PINMASK;
    }
    if (pinmask & DEFAULT_LFS_PINMASK) {
        LFS = &default_LFS;
    } else {
        LFS = calloc(1, sizeof(LFS_Wrapper));
    }
    v = LFS->get_vfs(fcfg, do_format);
#ifdef _DEBUG_LFS
    __builtin_printf("get_lfs_vfs returned %x\n", (unsigned)v);
#endif    
    return v;
}

int
_mkfs_littlefs_flash(struct littlefs_flash_config *fcfg)
{
    LFS_Wrapper *LFS;
    if (!fcfg) {
        fcfg = &default_cfg;
        LFS = &default_LFS;
    } else {
        LFS = calloc(1, sizeof(LFS_Wrapper));
    }
    int r;
    if (!fcfg) {
        fcfg = &default_cfg;
    }
    r = LFS->v_mkfs(fcfg);
    return r;
}
