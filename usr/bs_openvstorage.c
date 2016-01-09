/*
 * Synchronous OpenvStorage backing store routine
 *
 * Copyright (C) 2016 iNuron NV
 * Author: Chrysostomos Nanakos <cnanakos@openvstorage.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */
#define _XOPEN_SOURCE 600

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static void set_medium_error(int *result, uint8_t *key, uint16_t *asc)
{
    *result = SAM_STAT_CHECK_CONDITION;
    *key = MEDIUM_ERROR;
    *asc = ASC_READ_ERROR;
}

static int bs_ovs_io(struct active_ovs *ai, OVSTGTCmd cmd, char *buf, int len,
                     uint64_t offset)
{
    int r;
    ovs_buffer_t *ovs_buf;

    ovs_buf = ovs_allocate(ai->ioctx, len);
    if (ovs_buf == NULL)
    {
        eprintf("%s: cannot allocate shm buffer (len: %d)\n",
                __func__,
                len);
        return -1;
    }

    switch (cmd)
    {
    case OVS_TGT_OP_WRITE:
        memcpy(ovs_buffer_data(ovs_buf), buf, len);
        r = ovs_write(ai->ioctx, ovs_buffer_data(ovs_buf), len, offset);
        if (r < 0)
        {
            eprintf("%s: write failed (errno: %d)\n", __func__, errno);
        }
        break;
    case OVS_TGT_OP_READ:
        r = ovs_read(ai->ioctx, ovs_buffer_data(ovs_buf), len, offset);
        if (r < 0)
        {
            eprintf("%s: write failed (errno: %d)\n", __func__, errno);
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
        eprintf("%s: unknown operation (cmd: %d)\n",
                __func__,
                cmd);
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

static int bs_openvstorage_open(struct scsi_lu *lu,
                                char *path,
                                int *fd,
                                uint64_t *size)
{
    struct active_ovs *ai = OVSP(lu);
    uint32_t blksize = 0;
    struct stat st;
    int r;

    ai->ioctx = ovs_ctx_init(path, O_RDWR);
    if (ai->ioctx == NULL)
    {
        eprintf("%s: cannot create OpenvStorage context (errno: %d)\n",
                __func__,
                errno);
        return -EIO;
    }

    r = ovs_stat(ai->ioctx, &st);
    if (r < 0)
    {
        eprintf("%s: cannot stat volume (errno: %d)\n",
                __func__,
                errno);
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

static void bs_openvstorage_close(struct scsi_lu *lu)
{
    int r;
    struct active_ovs *ai = OVSP(lu);

    r = ovs_ctx_destroy(ai->ioctx);
    if (r < 0)
    {
        eprintf("%s: cannot destroy OpenvStorage context (errno: %d)\n",
                __func__,
                errno);
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
    .bs_open = bs_openvstorage_open,
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
    register_backingstore_template(&openvstorage_bst);
}
