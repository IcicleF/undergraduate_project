#if !defined(INODE_H)
#define INODE_H

#include "common.h"
#include "config.h"
#include "alloctable.h"

#define INO_FILE        1
#define INO_DIR         2

struct galois_inode
{
    ino_t ino;
    size_t size;
    mode_t mode;
    struct timespec atime;
    struct timespec mtime;
    
    int name_len;
    char name[MAX_FILENAME_LEN];

    int type;
    struct galois_inode *parent;
    struct galois_inode *child;         /* Valid if type == INO_DIR */
    struct galois_inode *sibl_prev;
    struct galois_inode *sibl_next;

    int path_hash;
    struct galois_inode *hash_next;
};

extern struct alloc_table *inode_alt;
extern struct galois_inode *inode_root;
extern struct galois_inode **inode_hashtable;

int init_root(struct alloc_table *table);

struct galois_inode *find_inode(struct galois_inode *from, const char *path);
struct galois_inode *find_dir(struct galois_inode *from, const char *path);
const char *extract_filename(const char *path, int *len);
char *path_join(const char *path1, const char *path2);

int hash_path(const char *path, int len);
void add_to_hashtable(struct galois_inode *inode, const char *path, int len);
void remove_from_hashtable(struct galois_inode *inode);
struct galois_inode *find_inode_by_hash(const char *path, int len);

int __mkdir(const char *pathname, mode_t mode);
int __mknod(const char *pathname, mode_t mode);

#endif // INODE_H
