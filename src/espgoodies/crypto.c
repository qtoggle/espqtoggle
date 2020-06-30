
/*
 * Copyright 2019 The qToggle Team
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <mem.h>
#include <ctype.h>

#include "common.h"
#include "crypto.h"


#define HMAC_SHA256_BLOCK_LEN 64

#define IPAD 0x36
#define OPAD 0x5C

#define rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))
#define ror(value, bits) (((value) >> (bits)) | ((value) << (32 - (bits))))
#define blk0(i) (block->l[i] = (rol(block->l[i], 24) & 0xFF00FF00) | (rol(block->l[i], 8) & 0x00FF00FF))
#define blk(i) (block->l[i & 15] = \
                rol(block->l[(i + 13) & 15] ^ block->l[(i + 8) & 15] ^ block->l[(i + 2) & 15] ^ block->l[i & 15], 1))

#define R0(v, w, x, y, z, i) z += ((w & (x ^ y)) ^ y) + blk0(i) + 0x5A827999 + rol(v, 5); w = rol(w, 30);
#define R1(v, w, x, y, z, i) z += ((w & (x ^ y)) ^ y) + blk(i) + 0x5A827999 + rol(v, 5); w = rol(w, 30);
#define R2(v, w, x, y, z, i) z += (w ^ x ^ y) + blk(i) + 0x6ED9EBA1 + rol(v, 5); w = rol(w, 30);
#define R3(v, w, x, y, z, i) z += (((w | x) & y)|(w & x)) + blk(i) + 0x8F1BBCDC + rol(v, 5); w = rol(w, 30);
#define R4(v, w, x, y, z, i) z += (w ^ x ^ y) + blk(i) + 0xCA62C1D6 + rol(v, 5); w = rol(w, 30);

#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y ,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x)  (ror(x, 2) ^  ror(x, 13) ^ ror(x, 22))
#define EP1(x)  (ror(x, 6) ^  ror(x, 11) ^ ror(x, 25))
#define SIG0(x) (ror(x, 7) ^  ror(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ror(x, 17) ^ ror(x, 19) ^ ((x) >> 10))


typedef struct {

    uint8               data[64];
    uint32              datalen;
    uint64              bitlen;
    uint32              state[8];

} sha256_ctx_t;


static const uint32 sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static const char b64_table[] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
    'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
    'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
    'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
    'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
    'w', 'x', 'y', 'z', '0', '1', '2', '3',
    '4', '5', '6', '7', '8', '9', '-', '_'
};


ICACHE_FLASH_ATTR static void   sha1_transform(uint32 state[5], uint8 buffer[64]);
ICACHE_FLASH_ATTR static void   sha1_update(uint32 state[5],
                                            uint8 buffer[64],
                                            uint32 count[2],
                                            uint8* data,
                                            uint32 len);
ICACHE_FLASH_ATTR static uint8 *sha1_final(uint32 state[5], uint8 buffer[64], uint32 count[2]);

ICACHE_FLASH_ATTR static void   sha256_init(sha256_ctx_t *ctx);
ICACHE_FLASH_ATTR static void   sha256_transform(sha256_ctx_t *ctx, uint8 *data);
ICACHE_FLASH_ATTR static void   sha256_update(sha256_ctx_t *ctx, uint8 *data, uint32 len);
ICACHE_FLASH_ATTR static uint8 *sha256_final(sha256_ctx_t *ctx);


uint8 *sha1(uint8 *data, int len) {
    uint32 state[5];
    uint32 count[2];
    uint8 buffer[64];
    uint8 *digest, *digest_dup = malloc(SHA1_LEN);

    state[0] = 0x67452301;
    state[1] = 0xEFCDAB89;
    state[2] = 0x98BADCFE;
    state[3] = 0x10325476;
    state[4] = 0xC3D2E1F0;
    count[0] = count[1] = 0;

    sha1_update(state, buffer, count, data, len);
    digest = sha1_final(state, buffer, count);
    memcpy(digest_dup, digest, SHA1_LEN);

    return digest_dup;
}

char *sha1_hex(char *s) {
    uint8 *digest = sha1((uint8 *) s, strlen(s));
    char *hex_digest = bin2hex(digest, SHA1_LEN);
    free(digest);

    return hex_digest;
}

uint8 *sha256(uint8 *data, int len) {
    sha256_ctx_t ctx;
    uint8 *digest, *digest_dup = malloc(SHA256_LEN);

    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    digest = sha256_final(&ctx);
    memcpy(digest_dup, digest, SHA256_LEN);

    return digest_dup;
}

char *sha256_hex(char *s) {
    uint8 *digest = sha256((uint8 *) s, strlen(s));
    char *hex_digest = bin2hex(digest, SHA256_LEN);
    free(digest);

    return hex_digest;
}

uint8 *hmac_sha256(uint8 *data, int data_len, uint8 *key, int key_len) {
    sha256_ctx_t ctx;
    uint8 buffer[HMAC_SHA256_BLOCK_LEN];
    uint8 *digest, *digest_dup;
    int i;

    memset(buffer, 0, HMAC_SHA256_BLOCK_LEN);

    /* If key is larger than a block, we have to hash it */
    if (key_len > HMAC_SHA256_BLOCK_LEN) {
        digest = sha256(key, key_len);
        memcpy(buffer, digest, SHA256_LEN);
        free(digest);
    }
    else {
        memcpy(buffer, key, key_len);
    }

    for (i = 0; i < HMAC_SHA256_BLOCK_LEN; i++) {
        buffer[i] ^= IPAD;
    }

    sha256_init(&ctx);
    sha256_update(&ctx, buffer, HMAC_SHA256_BLOCK_LEN);

    while (data_len >= HMAC_SHA256_BLOCK_LEN) {
        sha256_update(&ctx, data, HMAC_SHA256_BLOCK_LEN);
        data = data + HMAC_SHA256_BLOCK_LEN;
        data_len -=  HMAC_SHA256_BLOCK_LEN;
    }

    if (data_len > 0) {
        sha256_update(&ctx, data, data_len);
    }

    for (i = 0; i < HMAC_SHA256_BLOCK_LEN; i++) {
        buffer[i] ^= IPAD ^ OPAD;
    }

    digest = sha256_final(&ctx);

    sha256_init(&ctx);
    sha256_update(&ctx, buffer, HMAC_SHA256_BLOCK_LEN);
    sha256_update(&ctx, digest, SHA256_LEN);

    digest = sha256_final(&ctx);

    digest_dup = malloc(SHA256_LEN);
    memcpy(digest_dup, digest, SHA256_LEN);

    return digest_dup;
}

char *hmac_sha256_hex(char *s, char *key) {
    uint8 *hmac = hmac_sha256((uint8 *) s, strlen(s), (uint8 *) key, strlen(key));
    char *hex_hmac = bin2hex(hmac, SHA256_LEN);
    free(hmac);

    return hex_hmac;
}

char *b64_encode(uint8 *data, int len, bool padding) {
    int i = 0;
    int j = 0;
    char *enc = malloc(1);
    int size = 0;
    uint8 buf[4];
    uint8 tmp[3];

    /* Parse until end of source */
    while (len--) {
        /* Read up to 3 bytes at a time into tmp */
        tmp[i++] = *(data++);

        /* If 3 bytes read then encode into but */
        if (i == 3) {
            buf[0] = (tmp[0] & 0xfc) >> 2;
            buf[1] = ((tmp[0] & 0x03) << 4) + ((tmp[1] & 0xf0) >> 4);
            buf[2] = ((tmp[1] & 0x0f) << 2) + ((tmp[2] & 0xc0) >> 6);
            buf[3] = tmp[2] & 0x3f;

            /* Allocate 4 new bytes for enc and then translate each encoded buffer part by index from the base 64 index
             * table into enc unsigned char array */
            enc = realloc(enc, size + 4);
            for (i = 0; i < 4; i++) {
                enc[size++] = b64_table[buf[i]];
            }

            /* Reset index */
            i = 0;
        }
    }

    /* Remainder */
    if (i > 0) {
        /* Fill tmp with 0 at most 3 times */
        for (j = i; j < 3; j++) {
            tmp[j] = 0;
        }

        /* Perform same codec as above */
        buf[0] = (tmp[0] & 0xfc) >> 2;
        buf[1] = ((tmp[0] & 0x03) << 4) + ((tmp[1] & 0xf0) >> 4);
        buf[2] = ((tmp[1] & 0x0f) << 2) + ((tmp[2] & 0xc0) >> 6);
        buf[3] = tmp[2] & 0x3f;

        /* Perform same write to enc with new allocation */
        for (j = 0; (j < i + 1); j++) {
            enc = realloc(enc, size + 1);
            enc[size++] = b64_table[buf[j]];
        }

        if (padding) {
            /* While there is still a remainder, append = to enc */
            while ((i++ < 3)) {
                enc = realloc(enc, size + 1);
                enc[size++] = '=';
            }
        }
    }

    /* Make sure we have enough space to add 0 character at end */
    enc = realloc(enc, size + 1);
    enc[size] = '\0';

    return enc;
}

uint8 *b64_decode(char *s) {
  int i = 0;
  int j = 0;
  int l, size = 0;
  int len = strlen(s);
  uint8 *dec = NULL;
  uint8 buf[3];
  uint8 tmp[4];

  dec = malloc(1);

    /* Parse until end of source */
    while (len--) {
        /* Break if char is = or not base64 char */
        if (s[j] == '=') {
            break;
        }

        if (!(isalnum((int) s[j]) || '+' == s[j] || '/' == s[j])) {
            break;
        }

        /* Read up to 4 bytes at a time into tmp */
        tmp[i++] = s[j++];

        /* If 4 bytes read then decode into buf **/
        if (i == 4) {
            /* Translate values in tmp from table */
            for (i = 0; i < 4; i++) {
                /* Find translation char in b64_table */
                for (l = 0; l < 64; l++) {
                    if (tmp[i] == b64_table[l]) {
                        tmp[i] = l;
                        break;
                    }
                }
            }

            /* Decode */
            buf[0] = (tmp[0] << 2) + ((tmp[1] & 0x30) >> 4);
            buf[1] = ((tmp[1] & 0xf) << 4) + ((tmp[2] & 0x3c) >> 2);
            buf[2] = ((tmp[2] & 0x3) << 6) + tmp[3];

            /* Write decoded buffer to dec */
            dec = realloc(dec, size + 3);
            for (i = 0; i < 3; i++) {
                dec[size++] = buf[i];
            }

            /* Reset */
            i = 0;
        }
    }

    /* Remainder */
    if (i > 0) {
        /* Fill tmp with 0 at most 4 times */
        for (j = i; j < 4; ++j) {
            tmp[j] = 0;
        }

        /* Translate remainder */
        for (j = 0; j < 4; j++) {
            /* Find translation char in b64_table */
            for (l = 0; l < 64; l++) {
                if (tmp[j] == b64_table[l]) {
                    tmp[j] = l;
                    break;
                }
            }
        }

        /* Decode remainder */
        buf[0] = (tmp[0] << 2) + ((tmp[1] & 0x30) >> 4);
        buf[1] = ((tmp[1] & 0xf) << 4) + ((tmp[2] & 0x3c) >> 2);
        buf[2] = ((tmp[2] & 0x3) << 6) + tmp[3];

        /* Write remainder decoded buffer to dec */
        dec = realloc(dec, size + (i - 1));
        for (j = 0; (j < i - 1); j++) {
            dec[size++] = buf[j];
        }
    }

    /* Make sure we have enough space to add 0 character at end */
    dec = realloc(dec, size + 1);
    dec[size] = 0;

    return dec;
}

char *bin2hex(uint8 *bin, int len) {
    char *hex = malloc(2 * len + 1);
    int i;

    for (i = 0; i < len; i++) {
        snprintf(hex + i * 2, 3, "%02x", bin[i]);
    }

    hex[2 * len] = 0;

    return hex;
}

uint8 *hex2bin(char *hex) {
    int i, v1, v2, len = strlen(hex) / 2;
    char c1, c2;
    uint8 *bin = malloc(len);

    for (i = 0; i < len; i++) {
        c1 = hex[2 * i];
        c2 = hex[2 * i + 1];
        if (c1 >= '0' && c1 <= '9') {
            v1 = c1 - '0';
        }
        else if (c1 >= 'A' && c1 <= 'F') {
            v1 = c1 - 'A' + 10;
        }
        else if (c1 >= 'a' && c1 <= 'f') {
            v1 = c1 - 'a' + 10;
        }
        else {
            v1 = 0;
        }

        if (c2 >= '0' && c2 <= '9') {
            v2 = c2 - '0';
        }
        else if (c2 >= 'A' && c2 <= 'F') {
            v2 = c2 - 'A' + 10;
        }
        else if (c2 >= 'a' && c2 <= 'f') {
            v2 = c2 - 'a' + 10;
        }
        else {
            v2 = 0;
        }

        bin[i] = v1 * 16 + v2;
    }

    return bin;
}



void sha1_transform(uint32 state[5], uint8 buffer[64]) {
    uint32 a, b, c, d, e;
    typedef union {
        uint8 c[64];
        uint32 l[16];
    } CHAR64LONG16;

    CHAR64LONG16 block[1];
    memcpy(block, buffer, 64);

    /* Copy state to working vars */
    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];

    /* 4 rounds of 20 operations each. Loop unrolled. */
    R0(a,b,c,d,e, 0); R0(e,a,b,c,d, 1); R0(d,e,a,b,c, 2); R0(c,d,e,a,b, 3);
    R0(b,c,d,e,a, 4); R0(a,b,c,d,e, 5); R0(e,a,b,c,d, 6); R0(d,e,a,b,c, 7);
    R0(c,d,e,a,b, 8); R0(b,c,d,e,a, 9); R0(a,b,c,d,e,10); R0(e,a,b,c,d,11);
    R0(d,e,a,b,c,12); R0(c,d,e,a,b,13); R0(b,c,d,e,a,14); R0(a,b,c,d,e,15);
    R1(e,a,b,c,d,16); R1(d,e,a,b,c,17); R1(c,d,e,a,b,18); R1(b,c,d,e,a,19);
    R2(a,b,c,d,e,20); R2(e,a,b,c,d,21); R2(d,e,a,b,c,22); R2(c,d,e,a,b,23);
    R2(b,c,d,e,a,24); R2(a,b,c,d,e,25); R2(e,a,b,c,d,26); R2(d,e,a,b,c,27);
    R2(c,d,e,a,b,28); R2(b,c,d,e,a,29); R2(a,b,c,d,e,30); R2(e,a,b,c,d,31);
    R2(d,e,a,b,c,32); R2(c,d,e,a,b,33); R2(b,c,d,e,a,34); R2(a,b,c,d,e,35);
    R2(e,a,b,c,d,36); R2(d,e,a,b,c,37); R2(c,d,e,a,b,38); R2(b,c,d,e,a,39);
    R3(a,b,c,d,e,40); R3(e,a,b,c,d,41); R3(d,e,a,b,c,42); R3(c,d,e,a,b,43);
    R3(b,c,d,e,a,44); R3(a,b,c,d,e,45); R3(e,a,b,c,d,46); R3(d,e,a,b,c,47);
    R3(c,d,e,a,b,48); R3(b,c,d,e,a,49); R3(a,b,c,d,e,50); R3(e,a,b,c,d,51);
    R3(d,e,a,b,c,52); R3(c,d,e,a,b,53); R3(b,c,d,e,a,54); R3(a,b,c,d,e,55);
    R3(e,a,b,c,d,56); R3(d,e,a,b,c,57); R3(c,d,e,a,b,58); R3(b,c,d,e,a,59);
    R4(a,b,c,d,e,60); R4(e,a,b,c,d,61); R4(d,e,a,b,c,62); R4(c,d,e,a,b,63);
    R4(b,c,d,e,a,64); R4(a,b,c,d,e,65); R4(e,a,b,c,d,66); R4(d,e,a,b,c,67);
    R4(c,d,e,a,b,68); R4(b,c,d,e,a,69); R4(a,b,c,d,e,70); R4(e,a,b,c,d,71);
    R4(d,e,a,b,c,72); R4(c,d,e,a,b,73); R4(b,c,d,e,a,74); R4(a,b,c,d,e,75);
    R4(e,a,b,c,d,76); R4(d,e,a,b,c,77); R4(c,d,e,a,b,78); R4(b,c,d,e,a,79);

    /* Add the working vars back into state */
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;

    /* Wipe variables */
    a = b = c = d = e = 0;

    memset(block, 0, sizeof(block));
}

void sha1_update(uint32 state[5], uint8 buffer[64], uint32 count[2], uint8* data, uint32 len) {
    uint32 i, j;

    j = count[0];
    if ((count[0] += len << 3) < j) {
        count[1]++;
    }
    count[1] += (len >> 29);
    j = (j >> 3) & 63;
    if ((j + len) > 63) {
        memcpy(&buffer[j], data, (i = 64 - j));
        sha1_transform(state, buffer);
        for (; i + 63 < len; i += 64) {
            sha1_transform(state, data + i);
        }
        j = 0;
    }
    else {
        i = 0;
    }

    memcpy(buffer + j, data + i, len - i);
}

uint8 *sha1_final(uint32 state[5], uint8 buffer[64], uint32 count[2]) {
    int i;
    uint8 c, final_count[8];
    static uint8 digest[SHA1_LEN];

    for (i = 0; i < 8; i++) {
        final_count[i] = (uint8) ((count[(i >= 4 ? 0 : 1)] >> ((3-(i & 3)) * 8) ) & 255);
    }
    c = 0200;
    sha1_update(state, buffer, count, &c, 1);
    while ((count[0] & 504) != 448) {
        c = 0000;
        sha1_update(state, buffer, count, &c, 1);
    }
    sha1_update(state, buffer, count, final_count, 8);  /* Should cause a sha1_transform() */
    for (i = 0; i < SHA1_LEN; i++) {
        digest[i] = (uint8) ((state[i >> 2] >> ((3 - (i & 3)) * 8) ) & 255);
    }
    
    return digest;
}

void sha256_init(sha256_ctx_t *ctx) {
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
}

void sha256_transform(sha256_ctx_t *ctx, uint8 *data) {
    uint32 a, b, c, d, e, f, g, h, i, j, t1, t2, m[64];

    for (i = 0, j = 0; i < 16; i++, j += 4) {
        m[i] = (data[j] << 24) | (data[j + 1] << 16) | (data[j + 2] << 8) | (data[j + 3]);
    }
    for (; i < 64; ++i) {
        m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (i = 0; i < 64; i++) {
        t1 = h + EP1(e) + CH(e, f, g) + sha256_k[i] + m[i];
        t2 = EP0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void sha256_update(sha256_ctx_t *ctx, uint8 *data, uint32 len) {
    uint32 i;

    for (i = 0; i < len; i++) {
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;
        if (ctx->datalen == 64) {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

uint8 *sha256_final(sha256_ctx_t *ctx) {
    uint32 i;
    static uint8 digest[SHA256_LEN];

    i = ctx->datalen;

    /* Pad whatever data is left in the buffer */
    if (ctx->datalen < 56) {
        ctx->data[i++] = 0x80;
        while (i < 56) {
            ctx->data[i++] = 0x00;
        }
    }
    else {
        ctx->data[i++] = 0x80;
        while (i < 64) {
            ctx->data[i++] = 0x00;
        }
        sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56);
    }

    /* Append to the padding the total message's length in bits and transform */
    ctx->bitlen += ctx->datalen * 8;
    ctx->data[63] = ctx->bitlen;
    ctx->data[62] = ctx->bitlen >> 8;
    ctx->data[61] = ctx->bitlen >> 16;
    ctx->data[60] = ctx->bitlen >> 24;
    ctx->data[59] = ctx->bitlen >> 32;
    ctx->data[58] = ctx->bitlen >> 40;
    ctx->data[57] = ctx->bitlen >> 48;
    ctx->data[56] = ctx->bitlen >> 56;
    sha256_transform(ctx, ctx->data);

    /* Since this implementation uses little endian byte ordering and SHA uses big endian, reverse all the bytes when
     * copying the final state to the output hash */
    for (i = 0; i < 4; i++) {
        digest[i]      = (ctx->state[0] >> (24 - i * 8)) & 0x000000ff;
        digest[i + 4]  = (ctx->state[1] >> (24 - i * 8)) & 0x000000ff;
        digest[i + 8]  = (ctx->state[2] >> (24 - i * 8)) & 0x000000ff;
        digest[i + 12] = (ctx->state[3] >> (24 - i * 8)) & 0x000000ff;
        digest[i + 16] = (ctx->state[4] >> (24 - i * 8)) & 0x000000ff;
        digest[i + 20] = (ctx->state[5] >> (24 - i * 8)) & 0x000000ff;
        digest[i + 24] = (ctx->state[6] >> (24 - i * 8)) & 0x000000ff;
        digest[i + 28] = (ctx->state[7] >> (24 - i * 8)) & 0x000000ff;
    }

    return digest;
}
