#include "auth.h"
#include "protocol.h"
#include <stdio.h>
#include <string.h>

static uint32_t hash_name(const char *s)
{
    uint32_t h = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        h ^= *p;
        h *= 16777619u;
    }
    return h;
}

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static bool hex_decode(const char *hex, size_t hex_len, uint8_t *out, size_t out_len)
{
    if (hex_len != out_len * 2) {
        return false;
    }
    for (size_t i = 0; i < out_len; i++) {
        int hi = hex_val(hex[i * 2]);
        int lo = hex_val(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static void hex_encode(const uint8_t *in, size_t in_len, char *out)
{
    static const char HEX[] = "0123456789abcdef";
    for (size_t i = 0; i < in_len; i++) {
        out[i * 2]     = HEX[(in[i] >> 4) & 0x0Fu];
        out[i * 2 + 1] = HEX[in[i] & 0x0Fu];
    }
    out[in_len * 2] = '\0';
}

bool auth_valid_username(const char *name)
{
    if (!name) {
        return false;
    }
    size_t len = strlen(name);
    if (len < 3 || len > NAME_MAX_LEN) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        bool ok = (c >= 'A' && c <= 'Z') ||
                  (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') ||
                  (c == '_');
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool auth_valid_password(const char *pass)
{
    if (!pass) {
        return false;
    }
    size_t len = strlen(pass);
    if (len < 6 || len > 32) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)pass[i];
        if (c <= 0x20 || c == 0x7F || c == '|') {
            return false;
        }
    }
    return true;
}

void user_store_init(UserStore *us, const char *data_dir)
{
    if (!us) {
        return;
    }
    memset(us, 0, sizeof(*us));
    for (int i = 0; i < USER_STORE_CAP; i++) {
        us->slots[i].online.idx = CONN_IDX_NONE;
    }
    if (data_dir && data_dir[0] != '\0') {
        int n = snprintf(us->file_path, sizeof(us->file_path), "%s/users.dat", data_dir);
        if (n < 0 || (size_t)n >= sizeof(us->file_path)) {
            us->file_path[0] = '\0'; /* GCOVR_EXCL_LINE */
        }
    }
}

User *user_store_find(UserStore *us, const char *name)
{
    if (!us || !name || name[0] == '\0') {
        return NULL;
    }
    uint32_t mask = (uint32_t)(USER_STORE_CAP - 1);
    uint32_t idx = hash_name(name) & mask;
    for (int step = 0; step < USER_STORE_CAP; step++) {
        User *u = &us->slots[idx];
        if (!u->used) {
            return NULL;
        }
        if (strcmp(u->name, name) == 0) {
            return u;
        }
        idx = (idx + 1u) & mask;
    }
    return NULL; /* GCOVR_EXCL_LINE */
}

static User *insert_slot(UserStore *us, const char *name,
                         const uint8_t salt[SHA256_SALT_SIZE],
                         const uint8_t hash[SHA256_DIGEST_SIZE])
{
    if (us->count >= USER_STORE_CAP - 1) {
        return NULL;
    }
    uint32_t mask = (uint32_t)(USER_STORE_CAP - 1);
    uint32_t idx = hash_name(name) & mask;
    for (int step = 0; step < USER_STORE_CAP; step++) {
        User *u = &us->slots[idx];
        if (!u->used) {
            u->used = true;
            size_t nlen = strlen(name);
            memcpy(u->name, name, nlen + 1);
            memcpy(u->salt, salt, SHA256_SALT_SIZE);
            memcpy(u->hash, hash, SHA256_DIGEST_SIZE);
            u->online.idx = CONN_IDX_NONE;
            u->online.gen = 0;
            us->count++;
            return u;
        }
        if (strcmp(u->name, name) == 0) {
            return NULL;
        }
        idx = (idx + 1u) & mask;
    }
    return NULL; /* GCOVR_EXCL_LINE */
}

int user_store_load(UserStore *us)
{
    if (!us) {
        return -1;
    }
    if (us->file_path[0] == '\0') {
        return 0;
    }

    FILE *fp = fopen(us->file_path, "r");
    if (!fp) {
        return 0;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp) != NULL) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0) {
            continue;
        }

        char *c1 = strchr(line, ':');
        if (!c1) {
            continue;
        }
        *c1 = '\0';
        char *c2 = strchr(c1 + 1, ':');
        if (!c2 || strchr(c2 + 1, ':') != NULL) {
            continue;
        }
        *c2 = '\0';

        const char *name = line;
        const char *salt_hex = c1 + 1;
        const char *hash_hex = c2 + 1;

        uint8_t salt[SHA256_SALT_SIZE];
        uint8_t hash[SHA256_DIGEST_SIZE];
        if (!auth_valid_username(name)) {
            continue;
        }
        if (!hex_decode(salt_hex, strlen(salt_hex), salt, SHA256_SALT_SIZE)) {
            continue;
        }
        if (!hex_decode(hash_hex, strlen(hash_hex), hash, SHA256_DIGEST_SIZE)) {
            continue;
        }

        (void)insert_slot(us, name, salt, hash);
    }

    fclose(fp);
    return us->count;
}

int auth_register(UserStore *us, Rng *rng, const char *name, const char *pass)
{
    if (!us || !rng || !name || !pass) {
        return ERR_INTERNAL;
    }
    if (!auth_valid_username(name)) {
        return ERR_INVALID_USERNAME;
    }
    if (!auth_valid_password(pass)) {
        return ERR_INVALID_PASSWORD;
    }
    if (user_store_find(us, name) != NULL) {
        return ERR_USER_EXISTS;
    }
    if (us->count >= USER_STORE_CAP - 1) {
        return ERR_INTERNAL;
    }

    uint8_t salt[SHA256_SALT_SIZE];
    uint8_t hash[SHA256_DIGEST_SIZE];
    rng_bytes(rng, salt, SHA256_SALT_SIZE);
    sha256_salted(salt, pass, hash);

    if (us->file_path[0] != '\0') {
        FILE *fp = fopen(us->file_path, "a");
        if (!fp) {
            return ERR_INTERNAL;
        }
        char salt_hex[SHA256_SALT_SIZE * 2 + 1];
        char hash_hex[SHA256_HEX_SIZE];
        hex_encode(salt, SHA256_SALT_SIZE, salt_hex);
        sha256_to_hex(hash, hash_hex);
        int rc = fprintf(fp, "%s:%s:%s\n", name, salt_hex, hash_hex);
        fclose(fp);
        if (rc < 0) {
            return ERR_INTERNAL; /* GCOVR_EXCL_LINE */
        }
    }

    if (!insert_slot(us, name, salt, hash)) {
        return ERR_INTERNAL; /* GCOVR_EXCL_LINE */
    }
    return ERR_OK;
}

int auth_verify(UserStore *us, const char *name, const char *pass, User **out_user)
{
    if (out_user) {
        *out_user = NULL;
    }
    if (!us || !name || !pass) {
        return ERR_BAD_CREDENTIALS;
    }

    User *u = user_store_find(us, name);
    if (!u) {
        return ERR_BAD_CREDENTIALS;
    }

    uint8_t computed[SHA256_DIGEST_SIZE];
    sha256_salted(u->salt, pass, computed);

    uint8_t diff = 0;
    for (size_t i = 0; i < SHA256_DIGEST_SIZE; i++) {
        diff |= (uint8_t)(computed[i] ^ u->hash[i]);
    }
    if (diff != 0) {
        return ERR_BAD_CREDENTIALS;
    }

    if (out_user) {
        *out_user = u;
    }
    return ERR_OK;
}
