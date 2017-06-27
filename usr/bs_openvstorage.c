/*
 * Synchronous OpenvStorage backing store routine
 *
 * Copyright (C) 2016 iNuron NV
 * Author: Chrysostomos Nanakos <cnanakos@openvstorage.com>
 *
 * This file is part of Open vStorage Open Source Edition (OSE),
 * as available from
 *
 *     http://www.openvstorage.org and
 *     http://www.openvstorage.com.
 *
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License v3 (GNU AGPLv3)
 * as published by the Free Software Foundation, in version 3 as it comes in
 * the LICENSE.txt file of the Open vStorage OSE distribution.
 * Open vStorage is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY of any kind.
 */
#define _XOPEN_SOURCE 600

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <openvstorage/volumedriver.h>

#include "list.h"
#include "util.h"
#include "tgtd.h"
#include "scsi.h"
#include "spc.h"
#include "bs_thread.h"

#define MAX_REQUEST_SIZE	32768

#define OVS_DFL_NETWORK_PORT    21321
#define OVS_OPT_ENABLE_HA       "enable-ha"

struct active_ovs
{
    char *volume_name;
    ovs_ctx_t *ioctx;
};

typedef enum
{
    OVS_TGT_OP_READ,
    OVS_TGT_OP_WRITE,
} OVSTGTCmd;

#define OVSP(lu)    ((struct active_ovs *) \
                ((char*)lu + \
                 sizeof(struct scsi_lu) + \
                 sizeof(struct bs_thread_info)) \
        )

static int strstart(const char *str, const char *val, const char **ptr)
{
    const char *p, *q;
    p = str;
    q = val;
    while (*q != '\0') {
            if (*p != *q)
            {
                        return 0;
                    }
            p++;
            q++;
        }
    if (ptr)
    {
            *ptr = p;
        }
    return 1;
}

static void set_medium_error(int *result, uint8_t *key, uint16_t *asc)
{
    *result = SAM_STAT_CHECK_CONDITION;
    *key = MEDIUM_ERROR;
    *asc = ASC_READ_ERROR;
}

static int
ovs_chunked_write(struct active_ovs *ai, char *buf, int len, uint64_t offset)
{
    ssize_t remaining_len = len;
    ssize_t wsize = 0;
    ssize_t total = 0;
    while (remaining_len > 0)
    {
        ssize_t _wsize = MAX_REQUEST_SIZE;
        if (remaining_len < _wsize)
        {
            _wsize = remaining_len;
        }
        wsize = ovs_write(ai->ioctx, buf, _wsize, offset);
        if (wsize < 0)
        {
            return -1;
        }
        remaining_len -= wsize;
        offset += wsize;
        buf += wsize;
        total += wsize;
    }
    return total;
}

static int
ovs_chunked_read(struct active_ovs *ai, char *buf, int len, uint64_t offset)
{
    ssize_t remaining_len = len;
    ssize_t rsize = 0;
    ssize_t total = 0;
    while (remaining_len > 0)
    {
        ssize_t _rsize = MAX_REQUEST_SIZE;
        if (remaining_len < _rsize)
        {
            _rsize = remaining_len;
        }
        rsize = ovs_read(ai->ioctx, buf, _rsize, offset);
        if (rsize < 0)
        {
            return -1;
        }
        else if (rsize == 0)
        {
            return total;
        }
        remaining_len -= rsize;
        offset += rsize;
        buf += rsize;
        total += rsize;
    }
    return total;
}

static int bs_ovs_io(struct active_ovs *ai, OVSTGTCmd cmd, char *buf, int len,
                     uint64_t offset)
{
    int r;
    ovs_buffer_t *ovs_buf;

    ovs_buf = ovs_allocate(ai->ioctx, len);
    if (ovs_buf == NULL)
    {
        eprintf("%s: cannot allocate buffer (len: %d)\n",
                __func__,
                len);
        return -1;
    }

    switch (cmd)
    {
    case OVS_TGT_OP_WRITE:
        memcpy(ovs_buffer_data(ovs_buf), buf, len);
        r = ovs_chunked_write(ai, ovs_buffer_data(ovs_buf), len, offset);
        if (r < 0)
        {
            eprintf("%s: write failed (errno: %d)\n", __func__, errno);
        }
        break;
    case OVS_TGT_OP_READ:
        r = ovs_chunked_read(ai, ovs_buffer_data(ovs_buf), len, offset);
        if (r < 0)
        {
            eprintf("%s: read failed (errno: %d)\n", __func__, errno);
        }
        else
        {
            memcpy(buf, ovs_buffer_data(ovs_buf), r);
            if (r < len)
            {
                memset(buf + r, 0x0, len - r);
                r = len;
            }
        }
        break;
    default:
        eprintf("%s: unknown operation (cmd: %d)\n", __func__, cmd);
        r = -1;
    }

    ovs_deallocate(ai->ioctx, ovs_buf);
    return r;
}

static void bs_openvstorage_request(struct scsi_cmd *cmd)
{
    int r = 0;
    uint32_t length = 0;
    int result = SAM_STAT_GOOD;
    uint8_t key = 0;
    uint16_t asc = 0;
    struct active_ovs *ai = OVSP(cmd->dev);

    switch (cmd->scb[0])
    {
    case SYNCHRONIZE_CACHE:
    case SYNCHRONIZE_CACHE_16:
        r = ovs_flush(ai->ioctx);
        if (r < 0)
        {
            set_medium_error(&result, &key, &asc);
        }
        break;
    case WRITE_6:
    case WRITE_10:
    case WRITE_12:
    case WRITE_16:
        length = scsi_get_out_length(cmd);
        r = bs_ovs_io(ai,
                      OVS_TGT_OP_WRITE,
                      scsi_get_out_buffer(cmd),
                      length,
                      cmd->offset);
        if (r < 0)
        {
            set_medium_error(&result, &key, &asc);
        }
        break;
    case READ_6:
    case READ_10:
    case READ_12:
    case READ_16:
        length = scsi_get_in_length(cmd);
        r = bs_ovs_io(ai,
                      OVS_TGT_OP_READ,
                      scsi_get_in_buffer(cmd),
                      length,
                      cmd->offset);
        if (r < 0)
        {
            set_medium_error(&result, &key, &asc);
        }
        break;
    default:
        eprintf("cmd->scb[0]: %x\n", cmd->scb[0]);
        break;
    }

    dprintf("io done %p %x %d %u\n", cmd, cmd->scb[0], r, length);

    scsi_set_result(cmd, result);

    if (result != SAM_STAT_GOOD)
    {
         eprintf("io error %p %x %d %d %" PRIu64 ", %m\n",
                 cmd, cmd->scb[0], r, length, cmd->offset);
         sense_data_build(cmd, key, asc);
    }
}

static int bs_openvstorage_parse_path_opt(const char *filename,
                                          char **host,
                                          int *port,
                                          char **volume_name,
                                          int *enable_ha)
{
    const char *a;
    char *endptr, *inetaddr, *h;
    char *tokens[3], *ptoken, *ds;

    if (!filename)
    {
        eprintf("%s: invalid argument\n", __func__);
        return -1;
    }
    ds = strdup(filename);
    tokens[0] = strsep(&ds, "/");
    tokens[1] = strsep(&ds, ",");
    tokens[2] = strsep(&ds, "\0");


    if ((tokens[0] && !strlen(tokens[0])) ||
        (tokens[1] && !strlen(tokens[1])))
    {
        eprintf("%s: server and volume name must be specified", __func__);
        free(ds);
        return -1;
    }

    *volume_name = strdup(tokens[1]);
    if (!index(tokens[0], ':'))
    {
        *port = OVS_DFL_NETWORK_PORT;
        *host = strdup(tokens[0]);
    }
    else
    {
        inetaddr = strdup(tokens[0]);
        h = strtok(inetaddr, ":");
        if (h)
        {
            *host = strdup(h);
        }
        ptoken = strtok(NULL, "\0");
        if (ptoken != NULL)
        {
            int p = strtoul(ptoken, &endptr, 10);
            if (strlen(endptr))
            {
                eprintf("%s: server/port must be specified\n", __func__);
                free(inetaddr);
                free(ds);
                return -1;
            }
            *port = p;
        }
        else
        {
            eprintf("%s: server/port must be specified\n", __func__);
            free(inetaddr);
            free(ds);
            return -1;
        }
        free(inetaddr);
        free(ds);
    }
    if (tokens[2] != NULL && strstart(tokens[2], OVS_OPT_ENABLE_HA"=", &a))
    {
        if (strlen(a) > 0)
        {
            if (!strcmp(a, "on")) {
                *enable_ha = 1;
            } else if (!strcmp(a, "off")) {
                *enable_ha = 0;
            }
        } else {
            *enable_ha = 1;
        }
    }
    return 0;
}

static int bs_openvstorage_open_helper(struct scsi_lu *lu,
                                       const char *path,
                                       int *fd,
                                       uint64_t *size,
                                       const char *transport,
                                       bool is_network)
{
    struct active_ovs *ai = OVSP(lu);
    uint32_t blksize = 0;
    struct stat st;
    int r;
    char *host = NULL;
    char *volume_name = NULL;
    int port = 0;
    int enable_ha = 1;

    if (is_network)
    {
        r = bs_openvstorage_parse_path_opt(path,
                                           &host,
                                           &port,
                                           &volume_name,
                                           &enable_ha);
        if (r < 0)
        {
            return -EINVAL;
        }
    }

    ovs_ctx_attr_t *ctx_attr = ovs_ctx_attr_new();
    assert(ctx_attr != NULL);

    if (ovs_ctx_attr_set_transport(ctx_attr,
                                   transport,
                                   host,
                                   port) < 0) {
        r = -errno;
        eprintf("%s: cannot set transport type: %s\n",
                __func__,
                strerror(errno));
        ovs_ctx_attr_destroy(ctx_attr);
        return r;
    }

    if (enable_ha)
    {
        if (ovs_ctx_attr_enable_ha(ctx_attr) < 0) {
            r = -errno;
            eprintf("%s: cannot enable high availability: %s\n",
                    __func__,
                    strerror(errno));
            ovs_ctx_attr_destroy(ctx_attr);
            return r;
        }
    }

    ai->ioctx = ovs_ctx_new(ctx_attr);
    ovs_ctx_attr_destroy(ctx_attr);
    if (ai->ioctx == NULL)
    {
        eprintf("%s: cannot create context (errno: %d)\n", __func__, errno);
        return -EIO;
    }
    r = ovs_ctx_init(ai->ioctx, is_network ? volume_name : path, O_RDWR);
    if (r < 0)
    {
        r = -errno;
        eprintf("%s: cannot open volume: %s\n", __func__, strerror(errno));
        ovs_ctx_destroy(ai->ioctx);
        return r;
    }
    r = ovs_stat(ai->ioctx, &st);
    if (r < 0)
    {
        eprintf("%s: cannot stat volume (errno: %d)\n", __func__, errno);
        ovs_ctx_destroy(ai->ioctx);
        return r;
    }
    *size= st.st_size;
    blksize = st.st_blksize;

    lu->attrs.thinprovisioning = 1;

    if (!lu->attrs.no_auto_lbppbe)
    {
        update_lbppbe(lu, blksize);
    }
    return 0;

}

static int bs_openvstorage_open_shm(struct scsi_lu *lu,
                                    char *path,
                                    int *fd,
                                    uint64_t *size)
{
    return bs_openvstorage_open_helper(lu,
                                       path,
                                       fd,
                                       size,
                                       "shm",
                                       false);
}

static int bs_openvstorage_open_tcp(struct scsi_lu *lu,
                                    char *path,
                                    int *fd,
                                    uint64_t *size)
{
    return bs_openvstorage_open_helper(lu,
                                       path,
                                       fd,
                                       size,
                                       "tcp",
                                       true);
}

static int bs_openvstorage_open_rdma(struct scsi_lu *lu,
                                     char *path,
                                     int *fd,
                                     uint64_t *size)
{
    return bs_openvstorage_open_helper(lu,
                                       path,
                                       fd,
                                       size,
                                       "rdma",
                                       true);
}

static void bs_openvstorage_close(struct scsi_lu *lu)
{
    int r;
    struct active_ovs *ai = OVSP(lu);

    r = ovs_ctx_destroy(ai->ioctx);
    if (r < 0)
    {
        eprintf("%s: cannot destroy context (errno: %d)\n", __func__, errno);
    }
}

static tgtadm_err bs_openvstorage_init(struct scsi_lu *lu, char *bsopts)
{
    struct bs_thread_info *info = BS_THREAD_I(lu);
    return bs_thread_open(info, bs_openvstorage_request, nr_iothreads);
}

static void bs_openvstorage_exit(struct scsi_lu *lu)
{
    struct bs_thread_info *info = BS_THREAD_I(lu);
    bs_thread_close(info);
}

static struct backingstore_template openvstorage_bst = {
    .bs_name = "openvstorage",
    .bs_datasize = sizeof(struct bs_thread_info) + sizeof(struct active_ovs),
    .bs_open = bs_openvstorage_open_shm,
    .bs_close = bs_openvstorage_close,
    .bs_init = bs_openvstorage_init,
    .bs_exit = bs_openvstorage_exit,
    .bs_cmd_submit = bs_thread_cmd_submit,
    .bs_oflags_supported = O_SYNC | O_DIRECT,
};

static struct backingstore_template openvstorage_shm_bst = {
    .bs_name = "openvstorage+shm",
    .bs_datasize = sizeof(struct bs_thread_info) + sizeof(struct active_ovs),
    .bs_open = bs_openvstorage_open_shm,
    .bs_close = bs_openvstorage_close,
    .bs_init = bs_openvstorage_init,
    .bs_exit = bs_openvstorage_exit,
    .bs_cmd_submit = bs_thread_cmd_submit,
    .bs_oflags_supported = O_SYNC | O_DIRECT,
};

static struct backingstore_template openvstorage_tcp_bst = {
    .bs_name = "openvstorage+tcp",
    .bs_datasize = sizeof(struct bs_thread_info) + sizeof(struct active_ovs),
    .bs_open = bs_openvstorage_open_tcp,
    .bs_close = bs_openvstorage_close,
    .bs_init = bs_openvstorage_init,
    .bs_exit = bs_openvstorage_exit,
    .bs_cmd_submit = bs_thread_cmd_submit,
    .bs_oflags_supported = O_SYNC | O_DIRECT,
};

static struct backingstore_template openvstorage_rdma_bst = {
    .bs_name = "openvstorage+rdma",
    .bs_datasize = sizeof(struct bs_thread_info) + sizeof(struct active_ovs),
    .bs_open = bs_openvstorage_open_rdma,
    .bs_close = bs_openvstorage_close,
    .bs_init = bs_openvstorage_init,
    .bs_exit = bs_openvstorage_exit,
    .bs_cmd_submit = bs_thread_cmd_submit,
    .bs_oflags_supported = O_SYNC | O_DIRECT,
};

void register_bs_module(void)
{
    unsigned char opcodes[] = {
        ALLOW_MEDIUM_REMOVAL,
        FORMAT_UNIT,
        INQUIRY,
        MAINT_PROTOCOL_IN,
        MODE_SELECT,
        MODE_SELECT_10,
        MODE_SENSE,
        MODE_SENSE_10,
        PERSISTENT_RESERVE_IN,
        PERSISTENT_RESERVE_OUT,
        READ_10,
        READ_12,
        READ_16,
        READ_6,
        READ_CAPACITY,
        RELEASE,
        REPORT_LUNS,
        REQUEST_SENSE,
        RESERVE,
        SEND_DIAGNOSTIC,
        SERVICE_ACTION_IN,
        START_STOP,
        SYNCHRONIZE_CACHE,
        SYNCHRONIZE_CACHE_16,
        TEST_UNIT_READY,
        WRITE_10,
        WRITE_12,
        WRITE_16,
        WRITE_6
    };

    bs_create_opcode_map(&openvstorage_bst, opcodes, ARRAY_SIZE(opcodes));
    bs_create_opcode_map(&openvstorage_shm_bst, opcodes, ARRAY_SIZE(opcodes));
    bs_create_opcode_map(&openvstorage_tcp_bst, opcodes, ARRAY_SIZE(opcodes));
    bs_create_opcode_map(&openvstorage_rdma_bst, opcodes, ARRAY_SIZE(opcodes));
    register_backingstore_template(&openvstorage_bst);
    register_backingstore_template(&openvstorage_shm_bst);
    register_backingstore_template(&openvstorage_tcp_bst);
    register_backingstore_template(&openvstorage_rdma_bst);
}
