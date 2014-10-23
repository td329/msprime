/*
** Copyright (C) 2014 Jerome Kelleher <jerome.kelleher@well.ox.ac.uk>
**
** This file is part of msprime.
**
** msprime is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** msprime is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with msprime.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "err.h"
#include "msprime.h"

#define MSP_TREE_FILE_MAGIC 0xa52cd4a4
#define MSP_TREE_FILE_VERSION 1
#define MSP_TREE_FILE_HEADER_SIZE 28
#define MSP_NUM_CR_ELEMENTS 5

#define MSP_FLAGS_COMPLETE 1
#define MSP_FLAGS_SORTED 2


static int
cmp_coalescence_record(const void *a, const void *b) {
    const coalescence_record_t *ia = (const coalescence_record_t *) a;
    const coalescence_record_t *ib = (const coalescence_record_t *) b;
    return (ia->left > ib->left) - (ia->left < ib->left);
}

static inline void
decode_coalescence_record(uint32_t *record, coalescence_record_t *cr)
{
    union { float float_value; uint32_t uint_value; } conv;

    cr->left = record[0];
    cr->children[0] = (int32_t) record[1];
    cr->children[1] = (int32_t) record[2];
    cr->parent = (int32_t) record[3];
    conv.uint_value = record[4];
    cr->time = conv.float_value;
}

static inline void
encode_coalescence_record(coalescence_record_t *cr, uint32_t *record)
{
    union { float float_value; uint32_t uint_value; } conv;

    conv.float_value = cr->time;
    record[0] = cr->left;
    /* we do not record the right value as it's not needed when doing
     * complete simulations.
     */
    record[1] = (uint32_t) cr->children[0];
    record[2] = (uint32_t) cr->children[1];
    record[3] = (uint32_t) cr->parent;
    record[4] = conv.uint_value;
}

static inline int WARN_UNUSED
tree_file_check_read_mode(tree_file_t *self)
{
    return self->mode == 'r';
}

static inline int WARN_UNUSED
tree_file_check_write_mode(tree_file_t *self)
{
    return self->mode == 'w';
}

static inline int WARN_UNUSED
tree_file_check_update_mode(tree_file_t *self)
{
    return self->mode == 'u';
}

/*
 * Reads the header and metadata information from this file.
 */
static int WARN_UNUSED
tree_file_read_info(tree_file_t *self)
{
    int ret = -1;
    size_t metadata_size;
    uint32_t h32[5];
    uint64_t h64[1];

    assert((sizeof(h32) + sizeof(h64)) == MSP_TREE_FILE_HEADER_SIZE);
    /* read the header and get the basic information that we need */
    if (fread(h32, sizeof(h32), 1, self->file) != 1) {
        ret = MSP_ERR_IO;
        goto out;
    }
    if (fread(h64, sizeof(h64), 1, self->file) != 1) {
        ret = MSP_ERR_IO;
        goto out;
    }
    if (h32[0] != MSP_TREE_FILE_MAGIC) {
        ret = MSP_ERR_FILE_FORMAT;
        goto out;
    }
    if (h32[1] != MSP_TREE_FILE_VERSION) {
        ret = MSP_ERR_FILE_VERSION;
        goto out;
    }
    self->sample_size = h32[2];
    self->num_loci = h32[3];
    self->flags = h32[4];
    self->metadata_offset = h64[0];
    self->coalescence_record_offset = ftell(self->file);
    /* now read in the metadata */
    if (fseek(self->file, 0, SEEK_END) != 0) {
        ret = MSP_ERR_IO;
        goto out;
    }
    metadata_size = ftell(self->file) - self->metadata_offset;
    self->metadata = malloc(metadata_size + 1);
    if (self->metadata == NULL) {
        ret = MSP_ERR_NO_MEMORY;
        goto out;
    }
    if (fseek(self->file, self->metadata_offset, SEEK_SET) != 0) {
        ret = MSP_ERR_IO;
        goto out;
    }
    if (fread(self->metadata, metadata_size, 1, self->file) != 1) {
        ret = MSP_ERR_IO;
        goto out;
    }
    /* NULL terminate the string */
    self->metadata[metadata_size] = '\0';
    ret = 0;
out:
    return ret;
}

static int WARN_UNUSED
tree_file_write_header(tree_file_t *self)
{
    int ret = -1;
    uint32_t h32[5];
    uint64_t h64[1];

    assert((sizeof(h32) + sizeof(h64)) == MSP_TREE_FILE_HEADER_SIZE);
    h32[0] = MSP_TREE_FILE_MAGIC;
    h32[1] = MSP_TREE_FILE_VERSION;
    h32[2] = self->sample_size;
    h32[3] = self->num_loci;
    h32[4] = self->flags;
    h64[0] = self->metadata_offset;
    if (fseek(self->file, 0, SEEK_SET) != 0) {
        ret = MSP_ERR_IO;
        goto out;
    }
    if (fwrite(h32, sizeof(h32), 1, self->file) != 1) {
        ret = MSP_ERR_IO;
        goto out;
    }
    if (fwrite(h64, sizeof(h64), 1, self->file) != 1) {
        ret = MSP_ERR_IO;
        goto out;
    }
    ret = 0;
out:
    return ret;

}

static int WARN_UNUSED
tree_file_open_read_mode(tree_file_t *self)
{
    int ret = -1;

    self->file = fopen(self->filename, "r");
    if (self->file == NULL) {
        ret = MSP_ERR_IO;
        goto out;
    }
    ret = tree_file_read_info(self);
    if (ret != 0) {
        goto out;
    }
    /* now, seek back to the start of the record section so we're ready
     * to start reading records.
     */
    if (fseek(self->file, self->coalescence_record_offset, SEEK_SET) != 0) {
        ret = MSP_ERR_IO;
        goto out;
    }
    ret = 0;
out:
    return ret;
}

static int WARN_UNUSED
tree_file_open_write_mode(tree_file_t *self)
{
    int ret = -1;
    FILE *f = fopen(self->filename, "w");
    char *header = calloc(1, MSP_TREE_FILE_HEADER_SIZE);

    self->file = f;
    if (f == NULL) {
        ret = MSP_ERR_IO;
        goto out;
    }
    if (header == NULL) {
        ret = MSP_ERR_NO_MEMORY;
        goto out;
    }
    /* write a temporary header */
    if (fwrite(header, MSP_TREE_FILE_HEADER_SIZE, 1, f) != 1) {
        ret = MSP_ERR_IO;
        goto out;
    }
    ret = 0;
out:
    if (header != NULL) {
        free(header);
    }
    return ret;
}

static int WARN_UNUSED
tree_file_open_update_mode(tree_file_t *self)
{
    int ret = -1;

    self->file = fopen(self->filename, "r+");
    if (self->file == NULL) {
        ret = MSP_ERR_IO;
        goto out;
    }
    ret = tree_file_read_info(self);
    if (ret != 0) {
        goto out;
    }
    ret = 0;
out:
    return ret;
}


int
tree_file_print_state(tree_file_t *self)
{
    printf("Tree file '%s'\n", self->filename);
    printf("\tmode = %c\n", self->mode);
    printf("\tflags = %d\n", (int) self->flags);
    printf("\tsample_size = %d\n", (int) self->sample_size);
    printf("\tnum_loci = %d\n", (int) self->num_loci);
    printf("\tcoalescence_record_offset = %ld\n",
            (long) self->coalescence_record_offset);
    printf("\tmetadata_offset = %ld\n", (long) self->metadata_offset);
    printf("\tmetadata = '%s'\n", self->metadata);
    return 0;
}

int WARN_UNUSED
tree_file_open(tree_file_t *self, char *filename, char mode)
{
    int ret = -1;

    memset(self, 0, sizeof(tree_file_t));
    self->filename = malloc(1 + strlen(filename));
    if (self->filename == NULL) {
        goto out;
    }
    strcpy(self->filename, filename);
    self->mode = mode;
    if (mode == 'r') {
        ret = tree_file_open_read_mode(self);
    } else if (mode == 'w') {
        ret = tree_file_open_write_mode(self);
    } else if (mode == 'u') {
        ret = tree_file_open_update_mode(self);
    } else {
        ret = MSP_ERR_BAD_MODE;
    }
out:
    return ret;
}

int
tree_file_close(tree_file_t *self)
{
    if (self->filename != NULL) {
        free(self->filename);
    }
    if (self->metadata != NULL) {
        free(self->metadata);
    }
    if (self->file != NULL) {
        fclose(self->file);
    }
    return 0;
}

int WARN_UNUSED
tree_file_set_sample_size(tree_file_t *self, uint32_t sample_size)
{
    int ret = 0;

    if (!tree_file_check_write_mode(self)) {
        ret = MSP_ERR_BAD_MODE;
        goto out;
    }
    self->sample_size = sample_size;
out:
    return ret;
}

int WARN_UNUSED
tree_file_set_num_loci(tree_file_t *self, uint32_t num_loci)
{
    int ret = 0;

    if (!tree_file_check_write_mode(self)) {
        ret = MSP_ERR_BAD_MODE;
        goto out;
    }
    self->num_loci = num_loci;
out:
    return ret;
}

int WARN_UNUSED
tree_file_sort(tree_file_t *self)
{
    int ret = -1;
    FILE *f = self->file;
    coalescence_record_t *buff = NULL;
    size_t size = self->metadata_offset - self->coalescence_record_offset;
    size_t num_records = size / sizeof(coalescence_record_t);

    if (!tree_file_check_update_mode(self)) {
        ret = MSP_ERR_BAD_MODE;
        goto out;
    }
    buff = malloc(size);
    if (buff == NULL) {
        ret = MSP_ERR_NO_MEMORY;
        goto out;
    }
    if (fseek(f, self->coalescence_record_offset, SEEK_SET) != 0) {
        ret = MSP_ERR_IO;
        goto out;
    }
    if (fread(buff, size, 1, f) != 1) {
        ret = MSP_ERR_IO;
        goto out;
    }
    /* sort the records and write the results back */
    qsort(buff, num_records, sizeof(coalescence_record_t),
            cmp_coalescence_record);
    if (fseek(f, self->coalescence_record_offset, SEEK_SET) != 0) {
        ret = MSP_ERR_IO;
        goto out;
    }
    if (fwrite(buff, size, 1, f) != 1) {
        ret = MSP_ERR_IO;
        goto out;
    }
    /* set the sorted flag and write the header back */
    self->flags |= MSP_FLAGS_SORTED;
    ret = tree_file_write_header(self);
out:
    if (buff != NULL) {
        free(buff);
    }
    return ret;
}

/* Updates the file header so that different sections can be found
 * and then writes a footer.
 */
int WARN_UNUSED
tree_file_finalise(tree_file_t *self, msp_t *msp)
{
    int ret = -1;
    FILE *f = self->file;

    if (!tree_file_check_write_mode(self)) {
        ret = MSP_ERR_BAD_MODE;
        goto out;
    }
    self->metadata_offset = ftell(f);
    ret = msp_write_metadata(msp, f);
    if (ret != 0) {
        goto out;
    }
    self->flags |= MSP_FLAGS_COMPLETE;
    ret = tree_file_write_header(self);
    if (ret != 0) {
        goto out;
    }
    /* now we flush this, so that anyone looking at this file will
     * see the correct header and contents.
     */
    if (fflush(f) != 0) {
        ret = MSP_ERR_IO;
        goto out;
    }
    ret = 0;
out:
    return ret;
}


int WARN_UNUSED
tree_file_append_record(tree_file_t *self, coalescence_record_t *cr)
{
    int ret = -1;
    uint32_t record[MSP_NUM_CR_ELEMENTS];

    if (!tree_file_check_write_mode(self)) {
        ret = MSP_ERR_BAD_MODE;
        goto out;
    }
    encode_coalescence_record(cr, record);
    if (fwrite(&record, sizeof(record), 1, self->file) != 1) {
        ret = MSP_ERR_IO;
        goto out;
    }
    ret = 0;
out:
    return ret;
}

int WARN_UNUSED
tree_file_next_record(tree_file_t *self, coalescence_record_t *r)
{
    int ret = -1;
    FILE *f = self->file;
    uint32_t record[MSP_NUM_CR_ELEMENTS];

    if (!tree_file_check_read_mode(self)) {
        ret = MSP_ERR_BAD_MODE;
        goto out;
    }
    if (fread(record, sizeof(record), 1, f) != 1) {
        ret = MSP_ERR_IO;
        goto out;
    }
    decode_coalescence_record(record, r);
    ret = ftell(f) == self->metadata_offset ? 0 : 1;
out:
    return ret;
}

int WARN_UNUSED
tree_file_print_records(tree_file_t *self)
{
    int ret = 0;
    coalescence_record_t cr;

    while ((ret = tree_file_next_record(self, &cr)) == 1) {
        printf("%d\t(%d, %d)->%d @ %f\n", cr.left, cr.children[0],
                cr.children[1], cr.parent, cr.time);
    }
    return ret;
}

int
tree_file_is_complete(tree_file_t *self)
{
    return self->flags & MSP_FLAGS_COMPLETE;
}

int
tree_file_is_sorted(tree_file_t *self)
{
    return self->flags & MSP_FLAGS_SORTED;
}
