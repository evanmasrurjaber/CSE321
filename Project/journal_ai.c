#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>

#define BLOCK_SIZE 4096
#define FS_MAGIC 0x56534653      
#define JOURNAL_MAGIC 0x4A524E4C 
#define NAME_LEN 28
#define INODES_PER_BLOCK (BLOCK_SIZE / 128)
#define DIRENTS_PER_BLOCK (BLOCK_SIZE / 32)


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
    uint8_t  _pad[128 - (2+2+4 + 8*4 + 4+4)]; // padding to 128 bytes
};


struct dirent {
    uint32_t inode;            // inode number (0 = unused)
    char     name[NAME_LEN];   // filename, null-terminated if shorter
};


struct journal_header {
    uint32_t magic;         // JOURNAL_MAGIC
    uint32_t nbytes_used;   // bytes used; empty when == sizeof(header)
    uint8_t  _pad[BLOCK_SIZE - 8]; // rest of block reserved
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
    return (bitmap[byte_idx] & (1 << bit_idx)) != 0;
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


/* ===================== PHASE 2: FS Reader Functions ===================== */

void read_inode(const struct superblock *sb, uint32_t inum, struct inode *inode_out) {
    uint32_t inodes_per_block = BLOCK_SIZE / sizeof(struct inode);
    uint32_t block_index = inum / inodes_per_block;
    uint32_t offset_in_block = inum % inodes_per_block;
    
    uint8_t block_buf[BLOCK_SIZE];
    read_block_raw(sb->inode_start + block_index, block_buf);
    
    memcpy(inode_out, block_buf + offset_in_block * sizeof(struct inode), sizeof(struct inode));
}

void write_inode_to_buffer(uint8_t *block_buf, uint32_t offset_in_block, const struct inode *inode_in) {
    memcpy(block_buf + offset_in_block * sizeof(struct inode), inode_in, sizeof(struct inode));
}

int find_free_inode(const struct superblock *sb, const uint8_t *inode_bitmap) {
    for (uint32_t i = 0; i < sb->inode_count; i++) {
        if (!check_bit(inode_bitmap, i)) {
            return i;
        }
    }
    return -1;
}

int find_free_dirent_slot(const uint8_t *dir_block) {
    const struct dirent *entries = (const struct dirent *)dir_block;
    for (int i = 0; i < DIRENTS_PER_BLOCK; i++) {
        if (entries[i].inode == 0) {
            return i;
        }
    }
    return -1;
}

int find_dirent_by_name(const uint8_t *dir_block, const char *name) {
    const struct dirent *entries = (const struct dirent *)dir_block;
    for (int i = 0; i < DIRENTS_PER_BLOCK; i++) {
        if (entries[i].inode != 0 && strncmp(entries[i].name, name, NAME_LEN) == 0) {
            return i;
        }
    }
    return -1;
}


/* ===================== PHASE 3: Journal Functions ===================== */

void read_journal_header(const struct superblock *sb, struct journal_header *jh) {
    read_block_raw(sb->journal_block, jh);
}

void write_journal_header(const struct superblock *sb, const struct journal_header *jh) {
    write_block_raw(sb->journal_block, jh);
}

int append_data_record(const struct superblock *sb, struct journal_header *jh,
                       uint32_t dest_block, const uint8_t *block_data) {
    uint32_t record_size = sizeof(struct data_record);
    uint32_t journal_capacity = BLOCK_SIZE;  // Single block journal as per spec
    
    // Check if there's space in journal
    if (jh->nbytes_used + record_size > journal_capacity) {
        fprintf(stderr, "Journal full: cannot append data record\n");
        return -1;
    }
    
    // Prepare the data record
    struct data_record rec;
    rec.hdr.type = REC_DATA;
    rec.hdr.size = record_size;
    rec.block_no = dest_block;
    memcpy(rec.data, block_data, BLOCK_SIZE);
    
    // Read current journal block
    uint8_t journal_buf[BLOCK_SIZE];
    read_block_raw(sb->journal_block, journal_buf);
    
    // Append record at nbytes_used offset
    memcpy(journal_buf + jh->nbytes_used, &rec, record_size);
    jh->nbytes_used += record_size;
    
    // Update header in buffer
    memcpy(journal_buf, jh, sizeof(struct journal_header));
    
    // Write back
    write_block_raw(sb->journal_block, journal_buf);
    
    return 0;
}

int append_commit_record(const struct superblock *sb, struct journal_header *jh) {
    uint32_t record_size = sizeof(struct commit_record);
    uint32_t journal_capacity = BLOCK_SIZE;
    
    if (jh->nbytes_used + record_size > journal_capacity) {
        fprintf(stderr, "Journal full: cannot append commit record\n");
        return -1;
    }
    
    struct commit_record rec;
    rec.hdr.type = REC_COMMIT;
    rec.hdr.size = record_size;
    
    uint8_t journal_buf[BLOCK_SIZE];
    read_block_raw(sb->journal_block, journal_buf);
    
    memcpy(journal_buf + jh->nbytes_used, &rec, record_size);
    jh->nbytes_used += record_size;
    
    memcpy(journal_buf, jh, sizeof(struct journal_header));
    write_block_raw(sb->journal_block, journal_buf);
    
    return 0;
}


/* ===================== CREATE Command Implementation ===================== */

int do_create(const struct superblock *sb, const char *filename) {
    printf("Creating file: %s\n", filename);
    
    // Validate filename length
    if (strlen(filename) >= NAME_LEN) {
        fprintf(stderr, "Error: Filename too long (max %d chars)\n", NAME_LEN - 1);
        return -1;
    }
    
    // Step 1: Read inode bitmap
    uint8_t inode_bitmap[BLOCK_SIZE];
    read_bitmap_block(sb->inode_bitmap, inode_bitmap);
    
    // Step 2: Read root directory inode (inode 0 is root)
    struct inode root_inode;
    read_inode(sb, 0, &root_inode);
    
    if (root_inode.type != 2) {
        fprintf(stderr, "Error: Root inode is not a directory\n");
        return -1;
    }
    
    // Step 3: Read root directory data block
    uint32_t root_data_block = root_inode.direct[0];
    uint8_t dir_block[BLOCK_SIZE];
    read_block_raw(root_data_block, dir_block);
    
    // Step 4: Check if file already exists
    if (find_dirent_by_name(dir_block, filename) >= 0) {
        fprintf(stderr, "Error: File '%s' already exists\n", filename);
        return -1;
    }
    
    // Step 5: Find free directory slot
    int slot = find_free_dirent_slot(dir_block);
    if (slot < 0) {
        fprintf(stderr, "Error: Root directory is full\n");
        return -1;
    }
    
    // Step 6: Find free inode
    int new_inum = find_free_inode(sb, inode_bitmap);
    if (new_inum < 0) {
        fprintf(stderr, "Error: No free inodes available\n");
        return -1;
    }
    
    printf("  Allocated inode: %d\n", new_inum);
    printf("  Directory slot: %d\n", slot);
    
    // ===== Prepare modified blocks in memory =====
    
    // Modified inode bitmap
    uint8_t new_inode_bitmap[BLOCK_SIZE];
    memcpy(new_inode_bitmap, inode_bitmap, BLOCK_SIZE);
    set_bit(new_inode_bitmap, new_inum);
    
    // Modified inode block
    uint32_t inodes_per_block = BLOCK_SIZE / sizeof(struct inode);
    uint32_t inode_block_index = new_inum / inodes_per_block;
    uint32_t inode_offset = new_inum % inodes_per_block;
    uint32_t inode_block_num = sb->inode_start + inode_block_index;
    
    uint8_t inode_block[BLOCK_SIZE];
    read_block_raw(inode_block_num, inode_block);
    
    // Create new inode for the file
    struct inode new_inode;
    memset(&new_inode, 0, sizeof(struct inode));
    new_inode.type = 1;  // Regular file
    new_inode.links = 1;
    new_inode.size = 0;  // Empty file
    new_inode.ctime = (uint32_t)time(NULL);
    new_inode.mtime = new_inode.ctime;
    
    write_inode_to_buffer(inode_block, inode_offset, &new_inode);
    
    // Modified directory block
    uint8_t new_dir_block[BLOCK_SIZE];
    memcpy(new_dir_block, dir_block, BLOCK_SIZE);
    struct dirent *entries = (struct dirent *)new_dir_block;
    entries[slot].inode = new_inum;
    strncpy(entries[slot].name, filename, NAME_LEN - 1);
    entries[slot].name[NAME_LEN - 1] = '\0';
    
    // ===== Write to Journal =====
    
    struct journal_header jh;
    read_journal_header(sb, &jh);
    
    if (jh.magic != JOURNAL_MAGIC) {
        fprintf(stderr, "Error: Invalid journal magic\n");
        return -1;
    }
    
    // Reset journal for new transaction
    jh.nbytes_used = sizeof(struct journal_header);
    
    printf("  Writing to journal...\n");
    
    // Append DATA record for inode bitmap
    if (append_data_record(sb, &jh, sb->inode_bitmap, new_inode_bitmap) < 0) {
        return -1;
    }
    printf("    - Inode bitmap (block %u)\n", sb->inode_bitmap);
    
    // Append DATA record for inode block
    if (append_data_record(sb, &jh, inode_block_num, inode_block) < 0) {
        return -1;
    }
    printf("    - Inode block (block %u)\n", inode_block_num);
    
    // Append DATA record for directory block
    if (append_data_record(sb, &jh, root_data_block, new_dir_block) < 0) {
        return -1;
    }
    printf("    - Directory block (block %u)\n", root_data_block);
    
    // Append COMMIT record
    if (append_commit_record(sb, &jh) < 0) {
        return -1;
    }
    printf("    - Commit record\n");
    
    printf("  Journal transaction complete (bytes used: %u)\n", jh.nbytes_used);
    printf("File '%s' created successfully (pending install)\n", filename);
    
    return 0;
}


/* ===================== INSTALL Command Implementation ===================== */

int do_install(const struct superblock *sb) {
    printf("Installing journal transactions...\n");
    
    struct journal_header jh;
    read_journal_header(sb, &jh);
    
    if (jh.magic != JOURNAL_MAGIC) {
        fprintf(stderr, "Error: Invalid journal magic\n");
        return -1;
    }
    
    if (jh.nbytes_used <= sizeof(struct journal_header)) {
        printf("Journal is empty, nothing to install.\n");
        return 0;
    }
    
    // Read entire journal block
    uint8_t journal_buf[BLOCK_SIZE];
    read_block_raw(sb->journal_block, journal_buf);
    
    uint32_t offset = sizeof(struct journal_header);
    int data_records = 0;
    int commit_found = 0;
    
    // First pass: validate and count records
    uint32_t temp_offset = offset;
    while (temp_offset < jh.nbytes_used) {
        struct rec_header *hdr = (struct rec_header *)(journal_buf + temp_offset);
        
        if (hdr->type == REC_DATA) {
            data_records++;
            temp_offset += sizeof(struct data_record);
        } else if (hdr->type == REC_COMMIT) {
            commit_found = 1;
            break;
        } else {
            fprintf(stderr, "Error: Unknown record type %d\n", hdr->type);
            return -1;
        }
    }
    
    if (!commit_found) {
        printf("No commit record found, transaction incomplete. Aborting.\n");
        return -1;
    }
    
    printf("  Found %d data records with commit\n", data_records);
    
    // Second pass: replay DATA records
    while (offset < jh.nbytes_used) {
        struct rec_header *hdr = (struct rec_header *)(journal_buf + offset);
        
        if (hdr->type == REC_DATA) {
            struct data_record *rec = (struct data_record *)(journal_buf + offset);
            printf("  Applying block %u...\n", rec->block_no);
            write_block_raw(rec->block_no, rec->data);
            offset += sizeof(struct data_record);
        } else if (hdr->type == REC_COMMIT) {
            printf("  Commit record reached\n");
            break;
        }
    }
    
    // Clear journal (checkpoint)
    jh.nbytes_used = sizeof(struct journal_header);
    memset(journal_buf + sizeof(struct journal_header), 0, 
           BLOCK_SIZE - sizeof(struct journal_header));
    memcpy(journal_buf, &jh, sizeof(struct journal_header));
    write_block_raw(sb->journal_block, journal_buf);
    
    printf("Journal installed and cleared successfully.\n");
    return 0;
}


/* ===================== Main Function ===================== */

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <command> [args...] [image-path]\n", argv[0]);
        fprintf(stderr, "Commands: info | create <name> | install\n");
        return 1;
    }

    const char *image_path = "vsfs.img";
    
    // Determine image path based on command
    if (strcmp(argv[1], "create") == 0) {
        if (argc >= 4) image_path = argv[3];
    } else {
        if (argc >= 3) image_path = argv[argc-1];
    }

    open_disk(image_path);

    struct superblock sb;
    read_superblock(&sb);

    if (sb.magic != FS_MAGIC) {
        fprintf(stderr, "Error: Invalid FS magic: 0x%X (expected 0x%X)\n", sb.magic, FS_MAGIC);
        close_disk();
        return 1;
    }

    int result = 0;

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
        
        // Additional Phase 2 info
        printf("\nBitmap Analysis:\n");
        uint8_t inode_bitmap[BLOCK_SIZE];
        read_bitmap_block(sb.inode_bitmap, inode_bitmap);
        int used_inodes = 0;
        for (uint32_t i = 0; i < sb.inode_count; i++) {
            if (check_bit(inode_bitmap, i)) used_inodes++;
        }
        printf("  Used Inodes: %d / %u\n", used_inodes, sb.inode_count);
        
        int free_inode = find_free_inode(&sb, inode_bitmap);
        printf("  First Free Inode: %d\n", free_inode);
        
        // Show root directory contents
        printf("\nRoot Directory Contents:\n");
        struct inode root_inode;
        read_inode(&sb, 0, &root_inode);
        if (root_inode.type == 2 && root_inode.direct[0] != 0) {
            uint8_t dir_block[BLOCK_SIZE];
            read_block_raw(root_inode.direct[0], dir_block);
            struct dirent *entries = (struct dirent *)dir_block;
            for (int i = 0; i < DIRENTS_PER_BLOCK; i++) {
                if (entries[i].inode != 0) {
                    printf("  [%d] inode=%u name='%s'\n", i, entries[i].inode, entries[i].name);
                }
            }
        }
        
    } else if (strcmp(argv[1], "create") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s create <filename> [image-path]\n", argv[0]);
            close_disk();
            return 1;
        }
        result = do_create(&sb, argv[2]);
        
    } else if (strcmp(argv[1], "install") == 0) {
        result = do_install(&sb);
        
    } else {
        fprintf(stderr, "Unknown command: %s\n", argv[1]);
        result = 1;
    }

    close_disk();
    return result;
}