#if !defined(FUSE_USE_VERSION)
#define FUSE_USE_VERSION 39
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fuse.h>

#include <config.h>
#include <ecal.h>
#include <cluster.h>

struct node_config *my_node_conf;
atomic_bool running;

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
    int ret;
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    struct all_configs all_conf;

    init_all_configs(&all_conf, &args);
    ret = fuse_main(args.argc, args.argv, &galois_opers, NULL);
    destroy_all_configs(&all_conf, &args);

    return ret;
}