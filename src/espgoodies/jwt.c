
/*
 * Copyright (c) Calin Crisan
 * This file is part of espQToggle.
 *
 * espQToggle is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>. 
 */

#include <mem.h>

#include "common.h"
#include "crypto.h"
#include "jwt.h"


typedef uint8 *(*sign_func_t)(uint8 *data, int data_len, uint8 *key, int key_len);


jwt_t *jwt_new(uint8 alg, json_t *claims) {
    jwt_t *jwt = zalloc(sizeof(jwt_t));

    jwt->claims = json_dup(claims);
    jwt->alg = alg;

    return jwt;
}

void jwt_free(jwt_t *jwt) {
    json_free(jwt->claims);
    free(jwt);
}

char *jwt_dump(jwt_t *jwt, char *secret) {
    json_t *header = json_obj_new();
    json_obj_append(header, "typ", json_str_new("JWT"));

    sign_func_t sign_func;
    int signature_len;

    switch (jwt->alg) {
        case JWT_ALG_HS256:
            json_obj_append(header, "alg", json_str_new("HS256"));
            sign_func = hmac_sha256;
            signature_len = SHA256_LEN;

            break;

        default:
            json_free(header);
            return NULL;
    }

    char *header_str = json_dump(header, /* also_free = */ FALSE);
    json_free(header);
    char *header_b64 = b64_encode((uint8 *) header_str, strlen(header_str), /* padding = */ FALSE);
    free(header_str);

    char *payload_str = json_dump(jwt->claims, /* also_free = */ FALSE);
    char *payload_b64 = b64_encode((uint8 *) payload_str, strlen(payload_str), /* padding = */ FALSE);
    free(payload_str);

    int signing_len = strlen(header_b64) + strlen(payload_b64) + 1 /* dot */;
    char *signing_str = malloc(signing_len + 1);
    snprintf(signing_str, signing_len + 1, "%s.%s", header_b64, payload_b64);
    free(header_b64);
    free(payload_b64);

    uint8 *signature = sign_func((uint8 *) signing_str, signing_len, (uint8 *) secret, strlen(secret));
    char *signature_b64 = b64_encode(signature, signature_len, /* padding = */ FALSE);
    free(signature);

    int jwt_len = signing_len + strlen(signature_b64) + 1 /* dot */;
    char *jwt_str = malloc(jwt_len + 1);
    snprintf(jwt_str, jwt_len + 1, "%s.%s", signing_str, signature_b64);

    free(signing_str);
    free(signature_b64);

    return jwt_str;
}

jwt_t *jwt_parse(char *jwt_str) {
    /* look for the first dot */
    char *p1 = jwt_str;
    while (*p1 && *p1 != '.') {
        p1++;
    }
    if (!*p1) {
        /* no dot found */
        return NULL;
    }

    char *header_b64 = strndup(jwt_str, p1 - jwt_str);

    /* look for the second dot */
    char *p2 = p1 + 1;
    while (*p2 && *p2 != '.') {
        p2++;
    }
    if (!*p2) {
        /* no dot found */
        free(header_b64);
        return NULL;
    }

    /* parse header */
    char *header_str = (char *) b64_decode(header_b64);
    free(header_b64);

    json_t *header = json_parse(header_str);
    free(header_str);
    if (!header) {
        /* invalid json */
        return NULL;
    }

    json_t *typ_json = json_obj_lookup_key(header, "typ");
    if (!typ_json || json_get_type(typ_json) != JSON_TYPE_STR || strcmp(json_str_get(typ_json), "JWT")) {
        /* missing or invalid typ field */
        json_free(header);
        return NULL;
    }

    json_t *alg_json = json_obj_lookup_key(header, "alg");
    if (!alg_json || json_get_type(alg_json) != JSON_TYPE_STR) {
        /* missing or invalid alg field */
        json_free(header);
        return NULL;
    }

    char *alg_str = strdup(json_str_get(alg_json));
    json_free(header);  /* we don't need the header anymore */
    uint8 alg;

    if (!strcmp(alg_str, "HS256")) {
        alg = JWT_ALG_HS256;
    }
    else {
        /* unknown algorithm */
        free(alg_str);
        return NULL;
    }
    free(alg_str);

    /* parse payload */
    char *payload_b64 = strndup(p1 + 1, p2 - p1);
    char *payload_str = (char *) b64_decode(payload_b64);
    free(payload_b64);

    json_t *claims = json_parse(payload_str);
    free(payload_str);
    if (!claims) {
        /* invalid json */
        return NULL;
    }

    jwt_t *jwt = jwt_new(alg, claims);
    json_free(claims);

    return jwt;
}

bool jwt_validate(char *jwt_str, uint8 alg, char *secret) {
    /* look for the first dot */
    char *p = jwt_str;
    while (*p && *p != '.') {
        p++;
    }
    if (!*p) {
        /* no dot found */
        return FALSE;
    }

    /* look for the second dot */
    p++;
    while (*p && *p != '.') {
        p++;
    }
    if (!*p) {
        /* no dot found */
        return FALSE;
    }

    int signature_len;
    sign_func_t sign_func;

    switch (alg) {
        case JWT_ALG_HS256:
            sign_func = hmac_sha256;
            signature_len = SHA256_LEN;
            break;

        default:
            return FALSE;
    }

    /* compute the signing string */
    int signing_len = p - jwt_str;
    char *signing_str = strndup(jwt_str, signing_len);

    /* compute the local signature */
    uint8 *local_signature = sign_func((uint8 *) signing_str, signing_len, (uint8 *) secret, strlen(secret));
    free(signing_str);

    /* encode signature as base64 */
    char *local_signature_b64 = b64_encode(local_signature, signature_len, /* padding = */ FALSE);
    free(local_signature);

    char *signature_b64 = p + 1;
    /* remove any padding */
    int l = strlen(signature_b64);
    while (signature_b64[l - 1] == '=') {
        signature_b64[l - 1] = 0;
        l--;
    }
    bool valid = !strcmp(signature_b64, local_signature_b64);
    free(local_signature_b64);

    return valid;
}
