#if !defined(FUSE_USE_VERSION)
#define FUSE_USE_VERSION 39
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fuse.h>

#include <config.hpp>
#include <ecal.hpp>

static struct fuse_operations galois_opers = {
    /*
    .init           = galois_init,
    .destroy        = galois_destroy,

    .getattr        = galois_getattr,
    .readlink       = galois_readlink,
    .mknod          = galois_mknod,
    .mkdir          = galois_mkdir,
    .unlink         = galois_unlink,
    .rmdir          = galois_rmdir,
    .symlink        = galois_symlink,
    .rename         = galois_rename,
    .link           = galois_link,
    .chmod          = galois_chmod,
    .chown          = galois_chown,
    .truncate       = galois_truncate,
    .open           = galois_open,
    .read           = galois_read,
    .write          = galois_write,
    .statfs         = galois_truncate,
    .open           = galois_open,
    .read           = galois_read,
    .write          = galois_write,
    .statfs         = galois_statfs,
    .flush          = galois_flush,
    .release        = galois_release,
    .fsync          = galois_fsync,
    .setxattr       = galois_setxattr,
    .getxattr       = galois_getxattr,
    .listxattr      = galois_listxattr,
    .removexattr    = galois_removexattr,
    
    .readdir        = galois_readdir,
    .releasedir     = galois_releasedir,
    .fsyncdir       = galois_fsyncdir,

    .access         = galois_access,
    .create         = galois_create,
    .lock           = galois_lock,
    .utimens        = galois_utimens,
    .bmap           = galois_bmap,
    .ioctl          = galois_ioctl,
    .poll           = galois_poll,
    .write_buf      = galois_write_buf,
    .read_buf       = galois_read_buf,
    .flock          = galois_flock,
    .fallocate      = galois_fallocate,

    .copy_file_range = galois_copy_file_range,
    .lseek          = galois_lseek
    */
};

int main(int argc, char **argv)
{
    isRunning.store(true);

    // fuse_args args = FUSE_ARGS_INIT(argc, argv);
    
    cmdConf = new CmdLineConfig();
    cmdConf->setAsDefault();
    // cmdConf->initFromFuseArgs(&args);

    ECAL *ecal = new ECAL();
    uint64_t maxIndex = ecal->getClusterCapacity();

    srand(time(0));

    int indexes[10005];
    uint8_t bytes[10005];
    for (int i = 0; i < 10000; ++i) {
        int index = rand() % maxIndex;
        indexes[i] = index;

        ECAL::Page page(index);
        for (int j = 0; j < Block4K::size; j++)
            page.page.data[j] = rand() % 256;
        bytes[i] = page.page.data[1111];

        ecal->writeBlock(page);
    }

    int errs = 0;
    for (int i = 0; i < 10000; ++i) {
        ECAL::Page page = ecal->readBlock(indexes[i]);
        if (page.page.data[1111] != bytes[i])
            ++errs;
    }

    printf("Correct: %d, Wrong: %d\n", 10000 - errs, errs);

    delete ecal;
    delete cmdConf;

    return 0;
}