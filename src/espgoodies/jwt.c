
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

#include "espgoodies/common.h"
#include "espgoodies/crypto.h"
#include "espgoodies/jwt.h"


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

    char *header_str = json_dump_r(header, /* free_mode = */ JSON_FREE_NOTHING);
    json_free(header);
    char *header_b64 = b64_encode((uint8 *) header_str, strlen(header_str), /* padding = */ FALSE);

    char *payload_str = json_dump_r(jwt->claims, /* free_mode = */ JSON_FREE_NOTHING);
    char *payload_b64 = b64_encode((uint8 *) payload_str, strlen(payload_str), /* padding = */ FALSE);

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
    /* Look for the first dot */
    char *p1 = jwt_str;
    while (*p1 && *p1 != '.') {
        p1++;
    }
    if (!*p1) {
        /* No dot found */
        return NULL;
    }

    char *header_b64 = strndup(jwt_str, p1 - jwt_str);

    /* Look for the second dot */
    char *p2 = p1 + 1;
    while (*p2 && *p2 != '.') {
        p2++;
    }
    if (!*p2) {
        /* No dot found */
        free(header_b64);
        return NULL;
    }

    /* Parse header */
    char *header_str = (char *) b64_decode(header_b64);
    free(header_b64);

    json_t *header = json_parse(header_str);
    free(header_str);
    if (!header) {
        /* Invalid json */
        return NULL;
    }

    json_t *typ_json = json_obj_lookup_key(header, "typ");
    if (!typ_json || json_get_type(typ_json) != JSON_TYPE_STR || strcmp(json_str_get(typ_json), "JWT")) {
        /* Missing or invalid typ field */
        json_free(header);
        return NULL;
    }

    json_t *alg_json = json_obj_lookup_key(header, "alg");
    if (!alg_json || json_get_type(alg_json) != JSON_TYPE_STR) {
        /* Missing or invalid alg field */
        json_free(header);
        return NULL;
    }

    char *alg_str = strdup(json_str_get(alg_json));
    json_free(header);  /* We don't need the header anymore */
    uint8 alg;

    if (!strcmp(alg_str, "HS256")) {
        alg = JWT_ALG_HS256;
    }
    else {
        /* Unknown algorithm */
        free(alg_str);
        return NULL;
    }
    free(alg_str);

    /* Parse payload */
    char *payload_b64 = strndup(p1 + 1, p2 - p1);
    char *payload_str = (char *) b64_decode(payload_b64);
    free(payload_b64);

    json_t *claims = json_parse(payload_str);
    free(payload_str);
    if (!claims) {
        /* Invalid json */
        return NULL;
    }

    jwt_t *jwt = jwt_new(alg, claims);
    json_free(claims);

    return jwt;
}

bool jwt_validate(char *jwt_str, uint8 alg, char *secret) {
    /* Look for the first dot */
    char *p = jwt_str;
    while (*p && *p != '.') {
        p++;
    }
    if (!*p) {
        /* No dot found */
        return FALSE;
    }

    /* Look for the second dot */
    p++;
    while (*p && *p != '.') {
        p++;
    }
    if (!*p) {
        /* No dot found */
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

    /* Compute the signing string */
    int signing_len = p - jwt_str;
    char *signing_str = strndup(jwt_str, signing_len);

    /* Compute the local signature */
    uint8 *local_signature = sign_func((uint8 *) signing_str, signing_len, (uint8 *) secret, strlen(secret));
    free(signing_str);

    /* Encode signature as base64 */
    char *local_signature_b64 = b64_encode(local_signature, signature_len, /* padding = */ FALSE);
    free(local_signature);

    char *signature_b64 = p + 1;
    /* Remove any padding */
    int l = strlen(signature_b64);
    while (signature_b64[l - 1] == '=') {
        signature_b64[l - 1] = 0;
        l--;
    }
    bool valid = !strcmp(signature_b64, local_signature_b64);
    free(local_signature_b64);

    return valid;
}
