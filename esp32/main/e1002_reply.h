// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "cJSON.h"

// link.result transports only ok, payload and error. Keep hardware data in
// payload; otherwise wrap_result_json discards top-level result fields.
// Consumes result. Failures and acknowledgement-only replies stay unchanged.
static inline cJSON *e1002_data_reply(cJSON *result) {
    cJSON *ok = cJSON_GetObjectItemCaseSensitive(result, "ok");
    if (!cJSON_IsTrue(ok) || cJSON_GetArraySize(result) <= 1) return result;
    cJSON *reply = cJSON_CreateObject();
    if (!reply) {
        cJSON_Delete(result);
        return NULL;
    }
    ok = cJSON_DetachItemFromObjectCaseSensitive(result, "ok");
    if (!cJSON_AddItemToObject(reply, "ok", ok)) {
        cJSON_Delete(ok);
        cJSON_Delete(result);
        cJSON_Delete(reply);
        return NULL;
    }
    if (!cJSON_AddItemToObject(reply, "payload", result)) {
        cJSON_Delete(result);
        cJSON_Delete(reply);
        return NULL;
    }
    return reply;
}
