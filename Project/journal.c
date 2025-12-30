
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#define BLOCK_SIZE 4096
#define FS_MAGIC 0x56534653      
#define JOURNAL_MAGIC 0x4A524E4C 
#define NAME_LEN 28


struct superblock {
    uint32_t magic;         // FS magic number
    uint32_t block_size;    // 4096
    uint32_t total_blocks;  // total blocks in FS
    uint32_t inode_count;   // total inodes
    uint32_t journal_block; // block index of journal
    uint32_t inode_bitmap;  // inode bitmap block
    uint32_t data_bitmap;   // data bitmap block
    uint32_t inode_start;   // first inode block
    uint32_t data_start;    // first data block
    uint8_t  _pad[128 - 9 * 4]; // padding to 128 bytes
};


struct inode {
    uint16_t type;          // 0=free,1=file,2=dir
    uint16_t links;         // link count
    uint32_t size;          // file size bytes
    uint32_t direct[8];     // 8 direct pointers
    uint32_t ctime;         // creation time
    uint32_t mtime;         // modification time
    uint8_t  _pad[128 - (2+2+4 + 8*4 + 4+4)]; // nothing here
};


struct dirent {
    uint32_t inode;            // inode number (0 = unused)
    char     name[NAME_LEN];   // filename, null-terminated if shorter
};


struct journal_header {
    uint32_t magic;         // JOURNAL_MAGIC
    uint32_t nbytes_used;   // bytes used; empty when == sizeof(header)
    // rest of block reserved
};

#define REC_DATA 1
#define REC_COMMIT 2

struct rec_header {
    uint16_t type;   // REC_DATA or REC_COMMIT
    uint16_t size;   // total size of this record in bytes
};

struct data_record {
    struct rec_header hdr;        // REC_DATA
    uint32_t block_no;           // destination/home block number
    uint8_t data[BLOCK_SIZE];    // full block image
};

struct commit_record {
    struct rec_header hdr;       // REC_COMMIT
};


int disk_fd = -1;

void open_disk(const char *filename) {
    disk_fd = open(filename, O_RDWR);
    if (disk_fd < 0) {
        fprintf(stderr, "open_disk(%s) failed: %s\n", filename, strerror(errno));
        exit(1);
    }
}

void close_disk() {
    if (disk_fd >= 0) {
        close(disk_fd);
        disk_fd = -1;
    }
}


void read_block_raw(uint32_t block_num, void *buffer) {
    off_t offset = (off_t)block_num * (off_t)BLOCK_SIZE;
    if (lseek(disk_fd, offset, SEEK_SET) == (off_t)-1) {
        fprintf(stderr, "lseek read_block_raw failed: %s\n", strerror(errno));
        exit(1);
    }
    ssize_t r = read(disk_fd, buffer, BLOCK_SIZE);
    if (r != (ssize_t)BLOCK_SIZE) {
        fprintf(stderr, "read_block_raw: expected %d bytes, got %zd: %s\n",
                BLOCK_SIZE, r, (r < 0 ? strerror(errno) : "short read"));
        exit(1);
    }
}


void write_block_raw(uint32_t block_num, const void *buffer) {
    off_t offset = (off_t)block_num * (off_t)BLOCK_SIZE;
    if (lseek(disk_fd, offset, SEEK_SET) == (off_t)-1) {
        fprintf(stderr, "lseek write_block_raw failed: %s\n", strerror(errno));
        exit(1);
    }
    ssize_t w = write(disk_fd, buffer, BLOCK_SIZE);
    if (w != (ssize_t)BLOCK_SIZE) {
        fprintf(stderr, "write_block_raw: expected %d bytes, wrote %zd: %s\n",
                BLOCK_SIZE, w, (w < 0 ? strerror(errno) : "short write"));
        exit(1);
    }
}


void read_superblock(struct superblock *sb) {
    uint8_t buf[BLOCK_SIZE];
    read_block_raw(0, buf);
    memcpy(sb, buf, sizeof(struct superblock));
}


void read_bitmap_block(uint32_t bitmap_block_no, uint8_t *bitmap_out) {
    read_block_raw(bitmap_block_no, bitmap_out);
}


void write_bitmap_block(uint32_t bitmap_block_no, const uint8_t *bitmap_in) {
    write_block_raw(bitmap_block_no, bitmap_in);
}


int check_bit(const uint8_t *bitmap, int index) {
    int byte_idx = index / 8;
    int bit_idx = index % 8;
    return (bitmap[byte_idx] & (1 << bit_idx)) != 0; /*it will return 1 if the index in the bmap is used and 0 if not used*/
}

void set_bit(uint8_t *bitmap, int index) {
    int byte_idx = index / 8;
    int bit_idx = index % 8;
    bitmap[byte_idx] |= (1 << bit_idx);
}

void clear_bit(uint8_t *bitmap, int index) {
    int byte_idx = index / 8;
    int bit_idx = index % 8;
    bitmap[byte_idx] &= ~(1 << bit_idx);
}


int find_free_bit(const uint8_t *bitmap, int size_in_bits) {
    for (int i = 0; i < size_in_bits; i++) {
        if (!check_bit(bitmap, i)) return i;
    }
    return -1;
}


/*int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <command> [args...] [image-path]\n", argv[0]);
        fprintf(stderr, "Commands: info | create <name> | install\n");
        return 1;
    }

    const char *image_path = "vsfs.img";
    
    if (argc >= 4) image_path = argv[argc-1];

    open_disk(image_path);

    struct superblock sb;
    read_superblock(&sb);

    if (sb.magic != FS_MAGIC) {
        fprintf(stderr, "Error: Invalid FS magic: 0x%X (expected 0x%X)\n", sb.magic, FS_MAGIC);
        close_disk();
        return 1;
    }

    if (strcmp(argv[1], "info") == 0) {
        printf("Filesystem Info:\n");
        printf("  Magic: 0x%X\n", sb.magic);
        printf("  Block size (superblock field): %u\n", sb.block_size);
        printf("  Total Blocks: %u\n", sb.total_blocks);
        printf("  Inode Count: %u\n", sb.inode_count);
        printf("  Journal Block: %u\n", sb.journal_block);
        printf("  Inode Bitmap Block: %u\n", sb.inode_bitmap);
        printf("  Data Bitmap Block: %u\n", sb.data_bitmap);
        printf("  Inode Start Block: %u\n", sb.inode_start);
        printf("  Data Start Block: %u\n", sb.data_start);
    } else if (strcmp(argv[1], "create") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s create <filename>\n", argv[0]);
            close_disk();
            return 1;
        }
        
        printf("TODO: create '%s' (Phase 3 implementation goes here)\n", argv[2]);
    } else if (strcmp(argv[1], "install") == 0) {
        printf("TODO: install (Phase 4 implementation goes here)\n");
    } else {
        fprintf(stderr, "Unknown command: %s\n", argv[1]);
    }

    close_disk();
    return 0;
}*/
