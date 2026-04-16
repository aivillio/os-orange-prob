// object.c — Content-addressable object store
//
// Every piece of data (file contents, directory listings, commits) is stored
// as an "object" named by its SHA-256 hash. Objects are stored under
// .pes/objects/XX/YYYYYY... where XX is the first two hex characters of the
// hash (directory sharding).
//
// PROVIDED functions: compute_hash, object_path, object_exists, hash_to_hex, hex_to_hash
// TODO functions:     object_write, object_read

#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <openssl/evp.h>

static const char *object_type_name(ObjectType type) {
    switch (type) {
        case OBJ_BLOB: return "blob";
        case OBJ_TREE: return "tree";
        case OBJ_COMMIT: return "commit";
        default: return NULL;
    }
}

static int write_full(int fd, const uint8_t *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

static int fsync_dir(const char *path) {
    int dfd = open(path, O_RDONLY | O_DIRECTORY);
    if (dfd < 0) return -1;
    int rc = fsync(dfd);
    close(dfd);
    return rc;
}

static int parse_type_name(const char *s, ObjectType *type_out) {
    if (strcmp(s, "blob") == 0) {
        *type_out = OBJ_BLOB;
        return 0;
    }
    if (strcmp(s, "tree") == 0) {
        *type_out = OBJ_TREE;
        return 0;
    }
    if (strcmp(s, "commit") == 0) {
        *type_out = OBJ_COMMIT;
        return 0;
    }
    return -1;
}

// ─── PROVIDED ────────────────────────────────────────────────────────────────

void hash_to_hex(const ObjectID *id, char *hex_out) {
    for (int i = 0; i < HASH_SIZE; i++) {
        sprintf(hex_out + i * 2, "%02x", id->hash[i]);
    }
    hex_out[HASH_HEX_SIZE] = '\0';
}

int hex_to_hash(const char *hex, ObjectID *id_out) {
    if (strlen(hex) < HASH_HEX_SIZE) return -1;
    for (int i = 0; i < HASH_SIZE; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%2x", &byte) != 1) return -1;
        id_out->hash[i] = (uint8_t)byte;
    }
    return 0;
}

void compute_hash(const void *data, size_t len, ObjectID *id_out) {
    unsigned int hash_len;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, data, len);
    EVP_DigestFinal_ex(ctx, id_out->hash, &hash_len);
    EVP_MD_CTX_free(ctx);
}

// Get the filesystem path where an object should be stored.
// Format: .pes/objects/XX/YYYYYYYY...
// The first 2 hex chars form the shard directory; the rest is the filename.
void object_path(const ObjectID *id, char *path_out, size_t path_size) {
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);
    snprintf(path_out, path_size, "%s/%.2s/%s", OBJECTS_DIR, hex, hex + 2);
}

int object_exists(const ObjectID *id) {
    char path[512];
    object_path(id, path, sizeof(path));
    return access(path, F_OK) == 0;
}

// ─── TODO: Implement these ──────────────────────────────────────────────────

// Write an object to the store.
//
// Object format on disk:
//   "<type> <size>\0<data>"
//   where <type> is "blob", "tree", or "commit"
//   and <size> is the decimal string of the data length
//
// Steps:
//   1. Build the full object: header ("blob 16\0") + data
//   2. Compute SHA-256 hash of the FULL object (header + data)
//   3. Check if object already exists (deduplication) — if so, just return success
//   4. Create shard directory (.pes/objects/XX/) if it doesn't exist
//   5. Write to a temporary file in the same shard directory
//   6. fsync() the temporary file to ensure data reaches disk
//   7. rename() the temp file to the final path (atomic on POSIX)
//   8. Open and fsync() the shard directory to persist the rename
//   9. Store the computed hash in *id_out

// HINTS - Useful syscalls and functions for this phase:
//   - sprintf / snprintf : formatting the header string
//   - compute_hash       : hashing the combined header + data
//   - object_exists      : checking for deduplication
//   - mkdir              : creating the shard directory (use mode 0755)
//   - open, write, close : creating and writing to the temp file
//                          (Use O_CREAT | O_WRONLY | O_TRUNC, mode 0644)
//   - fsync              : flushing the file descriptor to disk
//   - rename             : atomically moving the temp file to the final path
//

//
// Returns 0 on success, -1 on error.
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out) {
    const char *type_name = object_type_name(type);
    if (!type_name || !data || !id_out) return -1;

    char header[64];
    int header_len = snprintf(header, sizeof(header), "%s %zu", type_name, len);
    if (header_len <= 0 || (size_t)header_len + 1 >= sizeof(header)) return -1;

    size_t full_len = (size_t)header_len + 1 + len;
    uint8_t *full_obj = malloc(full_len);
    if (!full_obj) return -1;

    memcpy(full_obj, header, (size_t)header_len);
    full_obj[header_len] = '\0';
    if (len > 0) memcpy(full_obj + header_len + 1, data, len);

    compute_hash(full_obj, full_len, id_out);
    if (object_exists(id_out)) {
        free(full_obj);
        return 0;
    }

    char final_path[512];
    char shard_dir[256];
    object_path(id_out, final_path, sizeof(final_path));

    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id_out, hex);
    snprintf(shard_dir, sizeof(shard_dir), "%s/%.2s", OBJECTS_DIR, hex);
    if (mkdir(shard_dir, 0755) != 0 && errno != EEXIST) {
        free(full_obj);
        return -1;
    }

    char tmp_path[544];
    snprintf(tmp_path, sizeof(tmp_path), "%s/.tmp-XXXXXX", shard_dir);

    int fd = mkstemp(tmp_path);
    if (fd < 0) {
        free(full_obj);
        return -1;
    }

    if (write_full(fd, full_obj, full_len) != 0 || fsync(fd) != 0) {
        close(fd);
        unlink(tmp_path);
        free(full_obj);
        return -1;
    }
    close(fd);

    if (rename(tmp_path, final_path) != 0) {
        unlink(tmp_path);
        free(full_obj);
        return -1;
    }

    if (fsync_dir(shard_dir) != 0) {
        free(full_obj);
        return -1;
    }

    free(full_obj);
    return 0;
}

// Read an object from the store.
//
// Steps:
//   1. Build the file path from the hash using object_path()
//   2. Open and read the entire file
//   3. Parse the header to extract the type string and size
//   4. Verify integrity: recompute the SHA-256 of the file contents
//      and compare to the expected hash (from *id). Return -1 if mismatch.
//   5. Set *type_out to the parsed ObjectType
//   6. Allocate a buffer, copy the data portion (after the \0), set *data_out and *len_out
//
// HINTS - Useful syscalls and functions for this phase:
//   - object_path        : getting the target file path
//   - fopen, fread, fseek: reading the file into memory
//   - memchr             : safely finding the '\0' separating header and data
//   - strncmp            : parsing the type string ("blob", "tree", "commit")
//   - compute_hash       : re-hashing the read data for integrity verification
//   - memcmp             : comparing the computed hash against the requested hash
//   - malloc, memcpy     : allocating and returning the extracted data
//
// The caller is responsible for calling free(*data_out).
// Returns 0 on success, -1 on error (file not found, corrupt, etc.).
int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out) {
    if (!id || !type_out || !data_out || !len_out) return -1;
    *data_out = NULL;
    *len_out = 0;

    char path[512];
    object_path(id, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long fsize = ftell(f);
    if (fsize <= 0) {
        fclose(f);
        return -1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }

    uint8_t *full = malloc((size_t)fsize);
    if (!full) {
        fclose(f);
        return -1;
    }

    if (fread(full, 1, (size_t)fsize, f) != (size_t)fsize) {
        free(full);
        fclose(f);
        return -1;
    }
    fclose(f);

    ObjectID actual;
    compute_hash(full, (size_t)fsize, &actual);
    if (memcmp(actual.hash, id->hash, HASH_SIZE) != 0) {
        free(full);
        return -1;
    }

    uint8_t *nul = memchr(full, '\0', (size_t)fsize);
    if (!nul) {
        free(full);
        return -1;
    }

    size_t header_len = (size_t)(nul - full);
    char header[128];
    if (header_len == 0 || header_len >= sizeof(header)) {
        free(full);
        return -1;
    }
    memcpy(header, full, header_len);
    header[header_len] = '\0';

    char type_name[16];
    size_t declared_size = 0;
    if (sscanf(header, "%15s %zu", type_name, &declared_size) != 2) {
        free(full);
        return -1;
    }
    if (parse_type_name(type_name, type_out) != 0) {
        free(full);
        return -1;
    }

    const uint8_t *payload = nul + 1;
    size_t payload_len = (size_t)fsize - (size_t)(payload - full);
    if (payload_len != declared_size) {
        free(full);
        return -1;
    }

    void *out = malloc(payload_len > 0 ? payload_len : 1);
    if (!out) {
        free(full);
        return -1;
    }
    if (payload_len > 0) memcpy(out, payload, payload_len);

    *data_out = out;
    *len_out = payload_len;

    free(full);
    return 0;
}
