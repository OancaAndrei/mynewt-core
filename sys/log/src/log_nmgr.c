/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <os/os.h>
#include <string.h>
#include <stdio.h>

#include "syscfg/syscfg.h"

#if MYNEWT_VAL(LOG_NEWTMGR)

#include "mgmt/mgmt.h"
#include "cborattr/cborattr.h"
#include "tinycbor/cbor_cnt_writer.h"
#include "log/log.h"

/* Source code is only included if the newtmgr library is enabled.  Otherwise
 * this file is compiled out for code size.
 */


static int log_nmgr_read(struct mgmt_cbuf *njb);
static int log_nmgr_clear(struct mgmt_cbuf *njb);
static int log_nmgr_module_list(struct mgmt_cbuf *njb);
static int log_nmgr_level_list(struct mgmt_cbuf *njb);
static int log_nmgr_logs_list(struct mgmt_cbuf *njb);
static struct mgmt_group log_nmgr_group;


/* ORDER MATTERS HERE.
 * Each element represents the command ID, referenced from newtmgr.
 */
static struct mgmt_handler log_nmgr_group_handlers[] = {
    [LOGS_NMGR_OP_READ] = {log_nmgr_read, log_nmgr_read},
    [LOGS_NMGR_OP_CLEAR] = {log_nmgr_clear, log_nmgr_clear},
    [LOGS_NMGR_OP_MODULE_LIST] = {log_nmgr_module_list, NULL},
    [LOGS_NMGR_OP_LEVEL_LIST] = {log_nmgr_level_list, NULL},
    [LOGS_NMGR_OP_LOGS_LIST] = {log_nmgr_logs_list, NULL}
};

struct encode_off {
    CborEncoder *eo_encoder;
    int64_t eo_ts;
    uint8_t eo_index;
    uint32_t rsp_len;
};

/**
 * Log encode entry
 * @param log structure, arg:struct passed locally, dataptr, len
 * @return 0 on success; non-zero on failure
 */
static int
log_nmgr_encode_entry(struct log *log, void *arg, void *dptr, uint16_t len)
{
    struct encode_off *encode_off = (struct encode_off *)arg;
    struct log_entry_hdr ueh;
    char data[128];
    int dlen;
    int rc;
    int rsp_len;
    CborError g_err = CborNoError;
    CborEncoder *penc = encode_off->eo_encoder;
    CborEncoder rsp;

    rc = log_read(log, dptr, &ueh, 0, sizeof(ueh));
    if (rc != sizeof(ueh)) {
        rc = OS_ENOENT;
        goto err;
    }
    rc = OS_OK;

    /* Matching timestamps and indices for sending a log entry */
    if (ueh.ue_ts < encode_off->eo_ts   ||
        (ueh.ue_ts == encode_off->eo_ts &&
         ueh.ue_index <= encode_off->eo_index)) {
        goto err;
    }

    dlen = min(len-sizeof(ueh), 128);

    rc = log_read(log, dptr, data, sizeof(ueh), dlen);
    if (rc < 0) {
        rc = OS_ENOENT;
        goto err;
    }
    data[rc] = 0;

    /*calculate whether this would fit */
    {
        /* create a counting encoder for cbor */
        struct CborCntWriter cnt_writer;
        CborEncoder cnt_encoder;
        cbor_cnt_writer_init(&cnt_writer);
        cbor_encoder_init(&cnt_encoder, &cnt_writer.enc, 0);

        /* NOTE This code should exactly match what is below */
        g_err |= cbor_encoder_create_map(&cnt_encoder, &rsp, CborIndefiniteLength);
        g_err |= cbor_encode_text_stringz(&rsp, "msg");
        g_err |= cbor_encode_text_stringz(&rsp, data);
        g_err |= cbor_encode_text_stringz(&rsp, "ts");
        g_err |= cbor_encode_int(&rsp, ueh.ue_ts);
        g_err |= cbor_encode_text_stringz(&rsp, "level");
        g_err |= cbor_encode_uint(&rsp, ueh.ue_level);
        g_err |= cbor_encode_text_stringz(&rsp, "index");
        g_err |= cbor_encode_uint(&rsp,  ueh.ue_index);
        g_err |= cbor_encode_text_stringz(&rsp, "module");
        g_err |= cbor_encode_uint(&rsp,  ueh.ue_module);
        g_err |= cbor_encoder_close_container(&cnt_encoder, &rsp);
        rsp_len = encode_off->rsp_len;
        rsp_len += cbor_encode_bytes_written(&cnt_encoder);

        if (rsp_len > MGMT_MAX_MTU) {
            rc = OS_ENOMEM;
            goto err;
        }
        encode_off->rsp_len = rsp_len;
    }

    g_err |= cbor_encoder_create_map(penc, &rsp, CborIndefiniteLength);
    g_err |= cbor_encode_text_stringz(&rsp, "msg");
    g_err |= cbor_encode_text_stringz(&rsp, data);
    g_err |= cbor_encode_text_stringz(&rsp, "ts");
    g_err |= cbor_encode_int(&rsp, ueh.ue_ts);
    g_err |= cbor_encode_text_stringz(&rsp, "level");
    g_err |= cbor_encode_uint(&rsp, ueh.ue_level);
    g_err |= cbor_encode_text_stringz(&rsp, "index");
    g_err |= cbor_encode_uint(&rsp,  ueh.ue_index);
    g_err |= cbor_encode_text_stringz(&rsp, "module");
    g_err |= cbor_encode_uint(&rsp,  ueh.ue_module);
    g_err |= cbor_encoder_close_container(penc, &rsp);

    return (0);
err:
    return (rc);
}

/**
 * Log encode entries
 * @param log structure, the encoder, timestamp, index
 * @return 0 on success; non-zero on failure
 */
static int
log_encode_entries(struct log *log, CborEncoder *cb,
                   int64_t ts, uint32_t index)
{
    int rc;
    struct encode_off encode_off;
    int rsp_len = 0;
    CborEncoder entries;
    CborError g_err = CborNoError;

    memset(&encode_off, 0, sizeof(encode_off));

    {
        /* this code counts how long the message would be if we encoded
         * this outer structure using cbor. */
        struct CborCntWriter cnt_writer;
        CborEncoder cnt_encoder;
        cbor_cnt_writer_init(&cnt_writer);
        cbor_encoder_init(&cnt_encoder, &cnt_writer.enc, 0);
        g_err |= cbor_encode_text_stringz(&cnt_encoder, "entries");
        g_err |= cbor_encoder_create_array(&cnt_encoder, &entries, CborIndefiniteLength);
        g_err |= cbor_encoder_close_container(&cnt_encoder, &entries);
        rsp_len = cbor_encode_bytes_written(cb)
                   + cbor_encode_bytes_written(&cnt_encoder);
        if (rsp_len > MGMT_MAX_MTU) {
            rc = OS_ENOMEM;
            goto err;
        }
    }

    g_err |= cbor_encode_text_stringz(cb, "entries");
    g_err |= cbor_encoder_create_array(cb, &entries, CborIndefiniteLength);

    encode_off.eo_encoder  = &entries;
    encode_off.eo_index    = index;
    encode_off.eo_ts       = ts;
    encode_off.rsp_len = rsp_len;

    rc = log_walk(log, log_nmgr_encode_entry, &encode_off);

    g_err |= cbor_encoder_close_container(cb, &entries);

err:
    return rc;
}

/**
 * Log encode function
 * @param log structure, the encoder, json_value,
 *        timestamp, index
 * @return 0 on success; non-zero on failure
 */
static int
log_encode(struct log *log, CborEncoder *cb,
            int64_t ts, uint32_t index)
{
    int rc;
    CborEncoder logs;
    CborError g_err = CborNoError;

    g_err |= cbor_encoder_create_map(cb, &logs, CborIndefiniteLength);
    g_err |= cbor_encode_text_stringz(&logs, "name");
    g_err |= cbor_encode_text_stringz(&logs, log->l_name);

    g_err |= cbor_encode_text_stringz(&logs, "type");
    g_err |= cbor_encode_uint(&logs, log->l_log->log_type);

    rc = log_encode_entries(log, &logs, ts, index);
    g_err |= cbor_encoder_close_container(cb, &logs);
    return rc;
}

/**
 * Newtmgr Log read handler
 * @param cbor buffer
 * @return 0 on success; non-zero on failure
 */
static int
log_nmgr_read(struct mgmt_cbuf *cb)
{
    struct log *log;
    int rc;
    char name[LOG_NAME_MAX_LEN] = {0};
    int name_len;
    int64_t ts;
    uint64_t index;
    CborError g_err = CborNoError;
    CborEncoder *penc = &cb->encoder;
    CborEncoder rsp, logs;

    const struct cbor_attr_t attr[4] = {
        [0] = {
            .attribute = "log_name",
            .type = CborAttrTextStringType,
            .addr.string = name,
            .len = sizeof(name)
        },
        [1] = {
            .attribute = "ts",
            .type = CborAttrIntegerType,
            .addr.integer = &ts
        },
        [2] = {
            .attribute = "index",
            .type = CborAttrUnsignedIntegerType,
            .addr.uinteger = &index
        },
        [3] = {
            .attribute = NULL
        }
    };

    rc = cbor_read_object(&cb->it, attr);
    if (rc) {
        return rc;
    }


    g_err |= cbor_encoder_create_map(penc, &rsp, CborIndefiniteLength);
    g_err |= cbor_encode_text_stringz(&rsp, "logs");

    g_err |= cbor_encoder_create_array(&rsp, &logs, CborIndefiniteLength);

    name_len = strlen(name);
    log = NULL;
    while (1) {
        log = log_list_get_next(log);
        if (!log) {
            break;
        }

        if (log->l_log->log_type == LOG_TYPE_STREAM) {
            continue;
        }

        /* Conditions for returning specific logs */
        if ((name_len > 0) && strcmp(name, log->l_name)) {
            continue;
        }

        rc = log_encode(log, &logs, ts, index);
        if (rc) {
            goto err;
        }

        /* If a log was found, encode and break */
        if (name_len > 0) {
            break;
        }
    }


    /* Running out of logs list and we have a specific log to look for */
    if (!log && name_len > 0) {
        rc = OS_EINVAL;
    }

err:
    g_err |= cbor_encoder_close_container(&rsp, &logs);
    g_err |= cbor_encode_text_stringz(&rsp, "rc");
    g_err |= cbor_encode_int(&rsp, rc);
    g_err |= cbor_encoder_close_container(penc, &rsp);

    rc = 0;
    return (rc);
}

/**
 * Newtmgr Module list handler
 * @param nmgr json buffer
 * @return 0 on success; non-zero on failure
 */
static int
log_nmgr_module_list(struct mgmt_cbuf *cb)
{
    int module;
    char *str;
    CborError g_err = CborNoError;
    CborEncoder *penc = &cb->encoder;
    CborEncoder rsp, modules;

    g_err |= cbor_encoder_create_map(penc, &rsp, CborIndefiniteLength);
    g_err |= cbor_encode_text_stringz(&rsp, "rc");
    g_err |= cbor_encode_int(&rsp, MGMT_ERR_EOK);

    g_err |= cbor_encode_text_stringz(&rsp, "module_map");
    g_err |= cbor_encoder_create_map(&rsp, &modules, CborIndefiniteLength);

    module = LOG_MODULE_DEFAULT;
    while (module < LOG_MODULE_MAX) {
        str = LOG_MODULE_STR(module);
        if (!strcmp(str, "UNKNOWN")) {
            module++;
            continue;
        }

        g_err |= cbor_encode_text_stringz(&modules, str);
        g_err |= cbor_encode_uint(&modules, module);
        module++;
    }

    g_err |= cbor_encoder_close_container(&rsp, &modules);
    g_err |= cbor_encoder_close_container(penc, &rsp);


    return (0);
}

/**
 * Newtmgr Log list handler
 * @param nmgr json buffer
 * @return 0 on success; non-zero on failure
 */
static int
log_nmgr_logs_list(struct mgmt_cbuf *cb)
{
    CborError g_err = CborNoError;
    CborEncoder *penc = &cb->encoder;
    CborEncoder rsp, log_list;
    struct log *log;

    g_err |= cbor_encoder_create_map(penc, &rsp, CborIndefiniteLength);
    g_err |= cbor_encode_text_stringz(&rsp, "rc");
    g_err |= cbor_encode_int(&rsp, MGMT_ERR_EOK);

    g_err |= cbor_encode_text_stringz(&rsp, "log_list");
    g_err |= cbor_encoder_create_array(&rsp, &log_list, CborIndefiniteLength);

    log = NULL;
    while (1) {
        log = log_list_get_next(log);
        if (!log) {
            break;
        }

        if (log->l_log->log_type == LOG_TYPE_STREAM) {
            continue;
        }

        g_err |= cbor_encode_text_stringz(&log_list, log->l_name);
    }

    g_err |= cbor_encoder_close_container(&rsp, &log_list);
    g_err |= cbor_encoder_close_container(penc, &rsp);

    return (0);
}

/**
 * Newtmgr Log Level list handler
 * @param nmgr json buffer
 * @return 0 on success; non-zero on failure
 */
static int
log_nmgr_level_list(struct mgmt_cbuf *cb)
{
    CborError g_err = CborNoError;
    CborEncoder *penc = &cb->encoder;
    CborEncoder rsp, level_map;
    int level;
    char *str;

    g_err |= cbor_encoder_create_map(penc, &rsp, CborIndefiniteLength);
    g_err |= cbor_encode_text_stringz(&rsp, "rc");
    g_err |= cbor_encode_int(&rsp, MGMT_ERR_EOK);

    g_err |= cbor_encode_text_stringz(&rsp, "level_map");
    g_err |= cbor_encoder_create_map(&rsp, &level_map, CborIndefiniteLength);

    level = LOG_LEVEL_DEBUG;
    while (level < LOG_LEVEL_MAX) {
        str = LOG_LEVEL_STR(level);
        if (!strcmp(str, "UNKNOWN")) {
            level++;
            continue;
        }

        g_err |= cbor_encode_text_stringz(&level_map, str);
        g_err |= cbor_encode_uint(&level_map, level);
        level++;
    }

    g_err |= cbor_encoder_close_container(&rsp, &level_map);
    g_err |= cbor_encoder_close_container(penc, &rsp);

    return (0);
}

/**
 * Newtmgr log clear handler
 * @param nmgr json buffer
 * @return 0 on success; non-zero on failure
 */
static int
log_nmgr_clear(struct mgmt_cbuf *cb)
{
    CborError g_err = CborNoError;
    CborEncoder *penc = &cb->encoder;
    CborEncoder rsp;
    struct log *log;
    int rc;

    log = NULL;
    while (1) {
        log = log_list_get_next(log);
        if (log == NULL) {
            break;
        }

        if (log->l_log->log_type == LOG_TYPE_STREAM) {
            continue;
        }

        rc = log_flush(log);
        if (rc) {
            goto err;
        }
    }
    g_err |= cbor_encoder_create_map(penc, &rsp, CborIndefiniteLength);
    g_err |= cbor_encoder_close_container(penc, &rsp);

    return 0;
err:
    mgmt_cbuf_setoerr(cb, rc);
    return (rc);
}

/**
 * Register nmgr group handlers.
 * @return 0 on success; non-zero on failure
 */
int
log_nmgr_register_group(void)
{
    int rc;

    MGMT_GROUP_SET_HANDLERS(&log_nmgr_group, log_nmgr_group_handlers);
    log_nmgr_group.mg_group_id = MGMT_GROUP_ID_LOGS;

    rc = mgmt_group_register(&log_nmgr_group);
    if (rc) {
        goto err;
    }

    return (0);
err:
    return (rc);
}

#endif
