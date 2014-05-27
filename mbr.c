/*
 * Copyright 2014 LKC Technologies, Inc.
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
 */

#include "mbr.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Hardcode the cylinder/head/sector geometry, since it's not relevant for
// the types of memory that we use.
#define SECTORS_PER_HEAD    63
#define HEADS_PER_CYLINDER  255

static const char *last_error = NULL;

/**
 * @brief mbr_verify check that the specified partitions make sense and don't overlap
 * @param partitions the partitions
 * @return 0 if successful
 */
int mbr_verify(const struct mbr_partition partitions[4])
{
    int i;
    // Check for overlap
    for (i = 0; i < 4; i++) {
        if (partitions[i].partition_type > 0xff || partitions[i].partition_type < 0) {
            last_error = "invalid partition type";
            return -1;
        }

        // Check if empty.
        if (partitions[i].partition_type == 0)
            continue;

        int ileft = partitions[i].block_offset;
        int iright = ileft + partitions[i].block_count - 1;
        int j;
        for (j = 0; j < 4; j++) {
            if (j == i)
                continue;

            int jleft = partitions[j].block_offset;
            int jright = jleft + partitions[j].block_count - 1;

            if ((ileft >= jleft && ileft <= jright) ||
                (iright >= jleft && iright <= jright)) {
                last_error = "partitions overlap";
                return -1;
            }
        }
    }

    return 0;
}

static int lba_to_chs(uint32_t lba, uint8_t *output)
{
    uint16_t cylinder = lba / (SECTORS_PER_HEAD * HEADS_PER_CYLINDER);
    uint8_t head = (uint8_t) ((lba / SECTORS_PER_HEAD) % HEADS_PER_CYLINDER);
    uint8_t sector = (uint8_t) (lba % SECTORS_PER_HEAD) + 1;

    output[0] = head;
    output[1] = ((cylinder & 0x300) >> 2) | sector;
    output[2] = (cylinder & 0xff);

    return 0;
}

static int create_partition(const struct mbr_partition *partition, uint8_t *output)
{
    output[0] = partition->boot_flag ? 0x80 : 0x00;

    if (lba_to_chs(partition->block_offset, &output[1]) < 0)
        return -1;

    output[4] = partition->partition_type;

    // Fill in the rest if this entry is actually used.
    if (partition->partition_type != 0) {
        if (lba_to_chs(partition->block_offset + partition->block_count - 1, &output[5]) < 0)
            return -1;

        output[8]  = partition->block_offset & 0xff;
        output[9]  = (partition->block_offset >> 8) & 0xff;
        output[10] = (partition->block_offset >> 16) & 0xff;
        output[11] = (partition->block_offset >> 24) & 0xff;

        output[12] = partition->block_count & 0xff;
        output[13] = (partition->block_count >> 8) & 0xff;
        output[14] = (partition->block_count >> 16) & 0xff;
        output[15] = (partition->block_count >> 24) & 0xff;
    }

    return 0;
}

/**
 * @brief Create a master boot record and put it in output
 * @param partitions the list of partitions
 * @param bootstrap optional bootstrap code (must be 440 bytes or NULL if none)
 * @param output the output location
 * @return 0 if success
 */
int mbr_create(const struct mbr_partition partitions[4], const uint8_t *bootstrap, uint8_t output[512])
{
    if (mbr_verify(partitions) < 0)
        return -1;

    memset(output, 0, 512);
    if (bootstrap)
        memcpy(output, bootstrap, 440);

    int i;
    for (i = 0; i < 4; i++) {
        if (create_partition(&partitions[i], &output[446 + i * 16]) < 0)
            return -1;
    }

    output[510] = 0x55;
    output[511] = 0xaa;
    return 0;
}

/**
 * @brief Return a string describing the most recent error return
 * @return a string or NULL if no error
 */
const char *mbr_last_error()
{
    return last_error;
}

static int read_partition(const uint8_t *input, struct mbr_partition *partition)
{
    if (input[0] & 0x80)
        partition->boot_flag = true;
    else
        partition->boot_flag = false;

    partition->partition_type = input[4];

    partition->block_offset =
            (input[8]) | (input[9] << 8) | (input[10] << 16) | (input[11] << 24);
    partition->block_count =
            (input[12]) | (input[13] << 8) | (input[14] << 16) | (input[15] << 24);

    return 0;
}

/**
 * @brief Decode the MBR data found in input
 * @param input the 512 byte MBR
 * @param partitions decoded data from the 4 partitions in the MBR
 * @return 0 if successful
 */
int mbr_decode(const uint8_t input[512], struct mbr_partition partitions[4])
{
    memset(partitions, 0, 4 * sizeof(struct mbr_partition));

    if (input[510] != 0x55 || input[511] != 0xaa) {
        last_error = "MBR signature missing";
        return -1;
    }

    int i;
    for (i = 0; i < 4; i++) {
        if (read_partition(&input[446 + i * 16], &partitions[i]) < 0)
            return -1;
    }

    return 0;
}
