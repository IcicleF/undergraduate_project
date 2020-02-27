#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <openssl/sha.h>

#include <inode.h>
#include <debug.h>

struct galois_inode *inode_root;
struct galois_inode **inode_hashtable;

int init_root(struct alloc_table *table)
{
    void *buf;
    struct galois_inode *root;
    long index;
    
    if (table->elem_size != sizeof(struct galois_inode)) {
        d_err("bad allocation table elem size (%d)", table->elem_size);
        return -1;
    }

    buf = alloc_elem(table, &index);
    if (index != 0) {
        d_err("#0 position is occupied");
        return -1;
    }
    
    root = (struct galois_inode *)buf;
    memset(root, 0, sizeof(struct galois_inode));

    root->ino = 1;
    root->size = sizeof(struct galois_inode);
    clock_gettime(CLOCK_REALTIME, &root->mtime);
    root->atime.tv_sec = root->mtime.tv_sec;
    root->atime.tv_nsec = root->mtime.tv_nsec;
    root->name_len = 0;
    root->type = INO_DIR;

    inode_root = root;
    inode_hashtable = calloc(sizeof(struct galois_inode *), INODE_HASH_SPACE);
    add_to_hashtable(root, "/", 1);

    return 0;
}

struct galois_inode *find_inode(struct galois_inode *from, const char *path)
{
    if (*path == '\0')
        return from;

    if (from == NULL)
        from = inode_root;
    if (path[0] == '/') {
        path++;
        from = inode_root;
    }

    while (from != NULL) {
        const char *end = path;
        int len;
        struct galois_inode *child = from->child;
        
        while (*end != '/' && *end != '\0')
            end++;
        len = end - path;

        while (child != NULL) {
            if (strncmp(child->name, path, len) == 0 && len == child->name_len)
                break;
            child = child->sibl_next;
        }
        from = child;
        
        if (*end == '\0')
            break;
        path = end + 1;
    }
    
    return from;
}

struct galois_inode *find_dir(struct galois_inode *from, const char *path)
{
    if (*path == '\0')
        return from->parent;            /* Returns the directory `from` is in */

    if (from == NULL)
        from = inode_root;
    if (path[0] == '/') {
        path++;
        from = inode_root;
    }

    while (from != NULL) {
        const char *end = path;
        int len;
        struct galois_inode *child = from->child;
        
        while (*end != '/' && *end != '\0')
            end++;
        if (*end == '\0' || *(end + 1) == '\0')     /* Handle /path/to/dir/ situations */
            break;
        
        len = end - path - 1;
        if (len > 0) {
            while (child != NULL) {
                if (strncmp(child->name, path, len) == 0 && len == child->name_len)
                    break;
                child = child->sibl_next;
            }
            from = child;
        }
        path = end + 1;
    }
    
    if (from->type != INO_DIR)
        d_err("find_dir found a non-dir inode (name: %s)", from->name);
    return from;
}

const char *extract_filename(const char *path)
{
    const char *res = path;
    while (*res != '\0') {
        const char *end = res;
        while (*end != '/' && *end != '\0')
            end++;
        if (end == '\0')
            return res;
        res = end + 1;
    }
    return NULL;
}

/**
 * This function joins the two paths together without checking their validity.
 * It allocates memory and gives it to user. User is responsible on freeing the memory.
 */
char *path_join(const char *path1, const char *path2)
{
    char *res;
    int len1 = strlen(path1), len2 = strlen(path2);

    if (*path2 == '/')          /* path2 is absolute */
        return strdup(path2);
    
    res = calloc(sizeof(char), len1 + len2 + 1);
    
    memcpy(res, path1, len1);
    if (path1[len1 - 1] != '/') {
        res[len1] = '/';
        len1 += 1;
    }
    memcpy(res + len1, path2, len2);
    return res;
}


int hash_path(const char *path, int len)
{
    static uint8_t hash[SHA_DIGEST_LENGTH];
    if (len <= 0)
        len = strlen(path);
    SHA1((const unsigned char *)path, len, hash);
    return ((int)hash[PICKED_BYTE0]) << 8 | hash[PICKED_BYTE1];
}

void add_to_hashtable(struct galois_inode *inode, const char *path, int len)
{
    if (inode->hash_next != NULL) {
        d_warn("inode seems to be in a hash table, abort");
        return;
    }

    int hash = hash_path(path, len);
    inode->hash_next = inode_hashtable[hash];
    inode_hashtable[hash] = inode;
}

void remove_from_hashtable(struct galois_inode *inode, const char *path, int len)
{
    if (inode->hash_next == NULL)
        return;
    
    int hash = hash_path(path, len);
    if (inode_hashtable[hash] == inode)
        inode_hashtable[hash] = inode->hash_next;
    else {
        struct galois_inode *ptr = inode_hashtable[hash];
        while (ptr != NULL) {
            if (ptr->hash_next == inode) {
                ptr->hash_next = inode->hash_next;
                break;
            }
            ptr = ptr->hash_next;
        }
    }
    inode->hash_next = NULL;
}

