#ifndef NETSKETCH_AUTH_H
#define NETSKETCH_AUTH_H

#include <stdbool.h>
#include <stdint.h>
#include "config.h"
#include "conn.h"
#include "rng.h"
#include "sha256.h"

#define USER_STORE_CAP 4096

typedef struct {
    bool    used;
    char    name[NAME_MAX_LEN + 1];
    uint8_t salt[SHA256_SALT_SIZE];
    uint8_t hash[SHA256_DIGEST_SIZE];
    ConnId  online;
} User;

typedef struct {
    User slots[USER_STORE_CAP];
    int  count;
    char file_path[256];
} UserStore;

bool  auth_valid_username(const char *name);
bool  auth_valid_password(const char *pass);

void  user_store_init(UserStore *us, const char *data_dir);
int   user_store_load(UserStore *us);
User *user_store_find(UserStore *us, const char *name);

int   auth_register(UserStore *us, Rng *rng, const char *name, const char *pass);
int   auth_verify(UserStore *us, const char *name, const char *pass, User **out_user);

#endif
