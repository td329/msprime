/*
** Copyright (C) 2015 Jerome Kelleher <jerome.kelleher@well.ox.ac.uk>
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
#include <sys/utsname.h>

#include <hdf5.h>

#include <gsl/gsl_math.h>
#include <gsl/gsl_randist.h>
#include <gsl/gsl_version.h>

#include "err.h"
#include "msprime.h"


typedef struct {
    uint32_t value;
    uint32_t index;
    double time;
} index_sort_t;

static int
cmp_mutation(const void *a, const void *b) {
    const mutation_t *ia = (const mutation_t *) a;
    const mutation_t *ib = (const mutation_t *) b;
    return (ia->position > ib->position) - (ia->position < ib->position);
}

static int
cmp_mutation_pointer(const void *a, const void *b) {
    mutation_t *const*ia = (mutation_t *const*) a;
    mutation_t *const*ib = (mutation_t *const*) b;
    return cmp_mutation(*ia, *ib);
}

static int
cmp_index_sort(const void *a, const void *b) {
    const index_sort_t *ca = (const index_sort_t *) a;
    const index_sort_t *cb = (const index_sort_t *) b;
    int ret = (ca->value > cb->value) - (ca->value < cb->value);
    /* When comparing equal values, we sort by time */
    if (ret == 0) {
        ret = (ca->time > cb->time) - (ca->time < cb->time);
    }
    return ret;
}

static int
encode_mutation_parameters(double mutation_rate, unsigned long random_seed,
        char **result)
{
    int ret = -1;
    const char *pattern = "{"
        "\"random_seed\":%lu,"
        "\"scaled_mutation_rate\":%.15f}";
    int written;
    size_t size = 1 + (size_t) snprintf(NULL, 0, pattern,
            random_seed,
            mutation_rate);
    char *str = malloc(size);

    if (str == NULL) {
        ret = MSP_ERR_NO_MEMORY;
        goto out;
    }
    written = snprintf(str, size, pattern,
            random_seed,
            mutation_rate);
    if (written < 0) {
        ret = MSP_ERR_IO;
        goto out;
    }
    assert(written == (int) size - 1);
    *result = str;
    ret = 0;
out:
    return ret;
}

/* Returns a JSON representation of the population models in the specified
 * result buffer. It is the calling code's responsibility to free this memory.
 */
static int
encode_population_models(msp_t *sim, char **result)
{
    int ret = -1;
    size_t num_models = msp_get_num_population_models(sim);
    size_t buffer_size = (num_models + 1) * 1024; /* 1K per model - should be plenty */
    size_t j, offset;
    int written;
    population_model_t *models = NULL;
    population_model_t *m;
    char *buffer = NULL;
    const char *param_name;
    const char *pattern = "{"
        "\"start_time\": %.15f,"
        "\"type\": %d,"
        "\"%s\": %.15f}";

    models = malloc(num_models * sizeof(population_model_t));
    if (models == NULL) {
        ret = MSP_ERR_NO_MEMORY;
        goto out;
    }
    ret = msp_get_population_models(sim, models);
    if (ret != 0) {
        goto out;
    }
    buffer = malloc(buffer_size);
    if (buffer == NULL) {
        ret = MSP_ERR_NO_MEMORY;
        goto out;
    }
    buffer[0] = '[';
    offset = 1;
    for (j = 0; j < num_models; j++) {
        m = &models[j];
        if (m->type == POP_MODEL_CONSTANT) {
            param_name = "size";
        } else if (m->type == POP_MODEL_EXPONENTIAL) {
            param_name = "alpha";
        } else {
            ret = MSP_ERR_BAD_POP_MODEL;
            goto out;
        }
        written = snprintf(buffer + offset, buffer_size - offset, pattern,
                m->start_time, m->type, param_name, m->param);
        if (written < 0) {
            ret = MSP_ERR_IO;
            goto out;
        }
        offset += (size_t) written;
        assert(offset < buffer_size - 1);
        if (j < num_models - 1) {
            buffer[offset] = ',';
            offset++;
        }
    }
    assert(offset < buffer_size - 2);
    buffer[offset] = ']';
    buffer[offset + 1] = '\0';
    *result = buffer;
out:
    if (models != NULL) {
        free(models);
    }
    return ret;
}

static int
encode_simulation_parameters(msp_t *sim, char **result)
{
    int ret = -1;
    const char *pattern = "{"
        "\"random_seed\":%lu,"
        "\"sample_size\":%d,"
        "\"num_loci\":%d,"
        "\"scaled_recombination_rate\":%.15f,"
        "\"population_models\":%s"
        "}";
    size_t size;
    int written;
    char *str = NULL;
    char *models = NULL;

    ret = encode_population_models(sim, &models);
    if (ret != 0) {
        goto out;
    }
    size = 1 + (size_t) snprintf(NULL, 0, pattern,
            sim->random_seed,
            sim->sample_size,
            sim->num_loci,
            sim->scaled_recombination_rate,
            models);
    str = malloc(size);
    if (str == NULL) {
        ret = MSP_ERR_NO_MEMORY;
        goto out;
    }
    written = snprintf(str, size, pattern,
            sim->random_seed,
            sim->sample_size,
            sim->num_loci,
            sim->scaled_recombination_rate,
            models);
    if (written < 0) {
        ret = MSP_ERR_IO;
        goto out;
    }
    assert(written == (int) size - 1);
    *result = str;
    ret = 0;
out:
    if (models != NULL) {
        free(models);
    }
    return ret;
}

static int
encode_environment(char **result)
{
    int ret = -1;
    /* TODO add more environment: endianess, and word size at a minimum */
    const char *pattern = "{"
        "\"msprime_version\":\"%s\", "
        "\"hdf5_version\":\"%d.%d.%d\", "
        "\"gsl_version\":\"%d.%d\", "
        "\"kernel_name\":\"%s\", "
        "\"kernel_release\":\"%s\", "
        "\"kernel_version\":\"%s\", "
        "\"hardware_identifier\":\"%s\""
        "}";
    herr_t status;
    unsigned int major, minor, release;
    int written;
    size_t size;
    char *str;
    struct utsname system_info;

    if (uname(&system_info) < 0) {
        ret = MSP_ERR_IO;
        goto out;
    }
    status = H5get_libversion(&major, &minor, &release);
    if (status != 0) {
        goto out;
    }
    size = 1 + (size_t) snprintf(NULL, 0, pattern,
            MSP_LIBRARY_VERSION_STR,
            major, minor, release,
            GSL_MAJOR_VERSION, GSL_MINOR_VERSION,
            system_info.sysname, system_info.release, system_info.version,
            system_info.machine);
    str = malloc(size);
    if (str == NULL) {
        ret = MSP_ERR_NO_MEMORY;
        goto out;
    }
    written = snprintf(str, size, pattern,
            MSP_LIBRARY_VERSION_STR,
            major, minor, release,
            GSL_MAJOR_VERSION, GSL_MINOR_VERSION,
            system_info.sysname, system_info.release, system_info.version,
            system_info.machine);
    if (written < 0) {
        ret = MSP_ERR_IO;
        goto out;
    }
    assert(written == (int) size - 1);
    *result = str;
    ret = 0;
out:
    return ret;
}

void
tree_sequence_print_state(tree_sequence_t *self)
{
    size_t j;

    printf("tree_sequence state\n");
    printf("sample_size = %d\n", self->sample_size);
    printf("num_loci = %d\n", self->num_loci);
    printf("trees = (%d records)\n", (int) self->num_records);
    printf("\tparameters = '%s'\n", self->trees.parameters);
    printf("\tenvironment = '%s'\n", self->trees.environment);
    for (j = 0; j < self->num_records; j++) {
        printf("\t%d\t%d\t%d\t%d\t%d\t%d\t%f\t|\t%d\t%d\n",
                (int) j,
                (int) self->trees.left[j],
                (int) self->trees.right[j],
                (int) self->trees.node[j],
                (int) self->trees.children[2 * j],
                (int) self->trees.children[2 * j + 1],
                self->trees.time[j],
                (int) self->trees.insertion_order[j],
                (int) self->trees.removal_order[j]);
    }
    printf("mutations = (%d records)\n", (int) self->num_mutations);
    printf("\tparameters = '%s'\n", self->mutations.parameters);
    printf("\tenvironment = '%s'\n", self->mutations.environment);
    for (j = 0; j < self->num_mutations; j++) {
        printf("\t%d\t%f\n", (int) self->mutations.node[j],
                self->mutations.position[j]);
    }

}

/* Allocates the memory required for arrays of values. Assumes that
 * the num_records and num_mutations have been set.
 */
static int
tree_sequence_alloc(tree_sequence_t *self)
{
    int ret = MSP_ERR_NO_MEMORY;

    self->trees.left = malloc(self->num_records * sizeof(uint32_t));
    self->trees.right = malloc(self->num_records * sizeof(uint32_t));
    self->trees.children = malloc(2 * self->num_records * sizeof(uint32_t));
    self->trees.node = malloc(self->num_records * sizeof(uint32_t));
    self->trees.time = malloc(self->num_records * sizeof(double));
    self->trees.insertion_order = malloc(self->num_records * sizeof(uint32_t));
    self->trees.removal_order = malloc(self->num_records * sizeof(uint32_t));
    if (self->trees.left == NULL || self->trees.right == NULL
            || self->trees.children == NULL || self->trees.node == NULL
            || self->trees.time == NULL || self->trees.insertion_order == NULL
            || self->trees.removal_order == NULL) {
        goto out;
    }
    if (self->num_mutations > 0) {
        self->mutations.node = malloc(self->num_mutations * sizeof(uint32_t));
        self->mutations.position = malloc(
                self->num_mutations * sizeof(double));
        if (self->mutations.node == NULL || self->mutations.position == NULL) {
            goto out;
        }
    }
    ret = 0;
out:
    return ret;
}

int
tree_sequence_free(tree_sequence_t *self)
{
    if (self->trees.left != NULL) {
        free(self->trees.left);
    }
    if (self->trees.right != NULL) {
        free(self->trees.right);
    }
    if (self->trees.children != NULL) {
        free(self->trees.children);
    }
    if (self->trees.node != NULL) {
        free(self->trees.node);
    }
    if (self->trees.time != NULL) {
        free(self->trees.time);
    }
    if (self->trees.insertion_order != NULL) {
        free(self->trees.insertion_order);
    }
    if (self->trees.removal_order != NULL) {
        free(self->trees.removal_order);
    }
    if (self->trees.parameters != NULL) {
        free(self->trees.parameters);
    }
    if (self->trees.environment != NULL) {
        free(self->trees.environment);
    }
    if (self->mutations.node != NULL) {
        free(self->mutations.node);
    }
    if (self->mutations.position != NULL) {
        free(self->mutations.position);
    }
    if (self->mutations.parameters != NULL) {
        free(self->mutations.parameters);
    }
    if (self->mutations.environment != NULL) {
        free(self->mutations.environment);
    }
    return 0;
}

static int
tree_sequence_make_indexes(tree_sequence_t *self)
{
    int ret = 0;
    uint32_t j;
    index_sort_t *sort_buff = NULL;

    sort_buff = malloc(self->num_records * sizeof(index_sort_t));
    if (sort_buff == NULL) {
        ret = MSP_ERR_NO_MEMORY;
        goto out;
    }
    /* sort by left and increasing time to give us the order in which
     * records should be inserted */
    for (j = 0; j < self->num_records; j++) {
        sort_buff[j].index = j;
        sort_buff[j].value = self->trees.left[j];
        sort_buff[j].time = self->trees.time[j];
    }
    qsort(sort_buff, self->num_records, sizeof(index_sort_t), cmp_index_sort);
    for (j = 0; j < self->num_records; j++) {
        self->trees.insertion_order[j] = sort_buff[j].index;
    }
    /* sort by right and decreasing time to give us the order in which
     * records should be removed. */
    for (j = 0; j < self->num_records; j++) {
        sort_buff[j].index = j;
        sort_buff[j].value = self->trees.right[j];
        sort_buff[j].time = -self->trees.time[j];
    }
    qsort(sort_buff, self->num_records, sizeof(index_sort_t), cmp_index_sort);
    for (j = 0; j < self->num_records; j++) {
        self->trees.removal_order[j] = sort_buff[j].index;
    }
    /* set the num_nodes value */
    self->num_nodes = self->trees.node[self->num_records - 1];
out:
    if (sort_buff != NULL) {
        free(sort_buff);
    }
    return ret;
}

int
tree_sequence_create(tree_sequence_t *self, msp_t *sim)
{
    int ret = -1;
    uint32_t j;
    coalescence_record_t *records = NULL;

    memset(self, 0, sizeof(tree_sequence_t));
    self->num_records = msp_get_num_coalescence_records(sim);
    assert(self->num_records > 0);
    self->sample_size = sim->sample_size;
    self->num_loci = sim->num_loci;
    self->num_mutations = 0;
    ret = tree_sequence_alloc(self);
    if (ret != 0) {
        goto out;
    }
    records = malloc(self->num_records * sizeof(coalescence_record_t));
    if (records == NULL) {
        goto out;
    }
    ret = msp_get_coalescence_records(sim, records);
    if (ret != 0) {
        goto out;
    }
    for (j = 0; j < self->num_records; j++) {
        self->trees.left[j] = records[j].left;
        self->trees.right[j] = records[j].right;
        self->trees.node[j] = records[j].node;
        self->trees.children[2 * j] = records[j].children[0];
        self->trees.children[2 * j + 1] = records[j].children[1];
        self->trees.time[j] = records[j].time;
    }
    ret = tree_sequence_make_indexes(self);
    if (ret != 0) {
        goto out;
    }
    /* Set the environment and parameter metadata */
    ret = encode_environment(&self->trees.environment);
    if (ret != 0) {
        goto out;
    }
    ret = encode_simulation_parameters(sim, &self->trees.parameters);
    if (ret != 0) {
        goto out;
    }
    ret = 0;
out:
    if (records != NULL) {
        free(records);
    }
    return ret;
}

/* Reads the metadata for the overall file and updates the basic
 * information in the tree_sequence.
 */
static int
tree_sequence_read_hdf5_metadata(tree_sequence_t *self, hid_t file_id)
{
    int ret = MSP_ERR_HDF5;
    hid_t attr_id, dataspace_id;
    herr_t status;
    int rank;
    hsize_t dims;
    int32_t version[2];
    struct _hdf5_metadata_read {
        const char *prefix;
        const char *name;
        hid_t memory_type;
        size_t size;
        void *dest;
    };
    struct _hdf5_metadata_read fields[] = {
        {"/", "format_version", H5T_NATIVE_UINT32, 2, NULL},
        {"/", "sample_size", H5T_NATIVE_UINT32, 0, NULL},
        {"/", "num_loci", H5T_NATIVE_UINT32, 0, NULL},
    };
    size_t num_fields = sizeof(fields) / sizeof(struct _hdf5_metadata_read);
    size_t j;

    fields[0].dest = version;
    fields[1].dest = &self->sample_size;
    fields[2].dest = &self->num_loci;
    for (j = 0; j < num_fields; j++) {
        attr_id = H5Aopen_by_name(file_id, fields[j].prefix, fields[j].name,
                H5P_DEFAULT, H5P_DEFAULT);
        if (attr_id < 0) {
            goto out;
        }
        dataspace_id = H5Aget_space(attr_id);
        if (dataspace_id < 0) {
            goto out;
        }
        rank = H5Sget_simple_extent_ndims(dataspace_id);
        if (fields[j].size == 0) {
            /* SCALAR's have rank 0 */
            if (rank != 0) {
                ret = MSP_ERR_FILE_FORMAT;
                goto out;
            }
        } else {
            if (rank != 1) {
                ret = MSP_ERR_FILE_FORMAT;
                goto out;
            }
            status = H5Sget_simple_extent_dims(dataspace_id, &dims, NULL);
            if (status < 0) {
                goto out;
            }
            if (dims != fields[j].size) {
                ret = MSP_ERR_FILE_FORMAT;
                goto out;
            }
        }
        status = H5Aread(attr_id, H5T_NATIVE_UINT32, fields[j].dest);
        if (status < 0) {
            goto out;
        }
        status = H5Sclose(dataspace_id);
        if (status < 0) {
            goto out;
        }
        status = H5Aclose(attr_id);
        if (status < 0) {
            goto out;
        }
    }
    /* Sanity check */
    if (version[0] != MSP_FILE_FORMAT_VERSION_MAJOR) {
        ret = MSP_ERR_UNSUPPORTED_FILE_VERSION;
        goto out;
    }
    ret = 0;
out:
    return ret;
}

static int
tree_sequence_check_hdf5_dimensions(tree_sequence_t *self, hid_t file_id)
{
    int ret = MSP_ERR_HDF5;
    hid_t dataset_id, dataspace_id;
    herr_t status;
    int rank;
    hsize_t dims[2];
    struct _dimension_check {
        const char *name;
        int dimensions;
        size_t size;
        int required;
    };
    struct _dimension_check fields[] = {
        {"/trees/left", 1, 0, 1},
        {"/trees/right", 1, 0, 1},
        {"/trees/node", 1, 0, 1},
        {"/trees/children", 2, 0, 1},
        {"/trees/time", 1, 0, 1},
        {"/mutations/node", 1, 0, 1},
        {"/mutations/position", 1, 0, 1},
    };
    size_t num_fields = sizeof(fields) / sizeof(struct _dimension_check);
    size_t j;

    for (j = 0; j < 5; j++) {
        fields[j].size = self->num_records;
    }
    for (j = 5; j < 7; j++) {
        fields[j].size = self->num_mutations;
        fields[j].required = self->num_mutations > 0;
    }
    for (j = 0; j < num_fields; j++) {
        if (fields[j].required) {
            dataset_id = H5Dopen(file_id, fields[j].name, H5P_DEFAULT);
            if (dataset_id < 0) {
                goto out;
            }
            dataspace_id = H5Dget_space(dataset_id);
            if (dataspace_id < 0) {
                goto out;
            }
            rank = H5Sget_simple_extent_ndims(dataspace_id);
            if (rank != fields[j].dimensions) {
                ret = MSP_ERR_FILE_FORMAT;
                goto out;
            }
            status = H5Sget_simple_extent_dims(dataspace_id, dims, NULL);
            if (status < 0) {
                goto out;
            }
            if (dims[0] != fields[j].size) {
                ret = MSP_ERR_FILE_FORMAT;
                goto out;
            }
            status = H5Sclose(dataspace_id);
            if (status < 0) {
                goto out;
            }
            status = H5Dclose(dataset_id);
            if (status < 0) {
                goto out;
            }
        }
    }
    ret = 0;
out:
    return ret;
}

/* Reads the dimensions for the records and mutations and mallocs
 * space.
 */
static int
tree_sequence_read_hdf5_dimensions(tree_sequence_t *self, hid_t file_id)
{
    int ret = MSP_ERR_HDF5;
    hid_t dataset_id, dataspace_id;
    herr_t status;
    htri_t exists;
    int rank;
    hsize_t dims[2];
    struct _dimension_read {
        const char *name;
        size_t *dest;
        int included;
    };
    struct _dimension_read fields[] = {
        {"/trees/left", NULL, 1},
        {"/mutations/node", NULL, 0},
    };
    size_t num_fields = sizeof(fields) / sizeof(struct _dimension_read);
    size_t j;

    fields[0].dest = &self->num_records;
    fields[1].dest = &self->num_mutations;
    /* check if the mutations group exists */
    exists = H5Lexists(file_id, "/mutations", H5P_DEFAULT);
    if (exists < 0) {
        goto out;
    }
    self->num_mutations = 0;
    if (exists) {
        fields[1].included = 1;
    }
    for (j = 0; j < num_fields; j++) {
        if (fields[j].included) {
            dataset_id = H5Dopen(file_id, fields[j].name, H5P_DEFAULT);
            if (dataset_id < 0) {
                goto out;
            }
            dataspace_id = H5Dget_space(dataset_id);
            if (dataspace_id < 0) {
                goto out;
            }
            rank = H5Sget_simple_extent_ndims(dataspace_id);
            if (rank != 1) {
                ret = MSP_ERR_FILE_FORMAT;
                goto out;
            }
            status = H5Sget_simple_extent_dims(dataspace_id, dims, NULL);
            if (status < 0) {
                goto out;
            }
            *fields[j].dest = (size_t) dims[0];
            status = H5Sclose(dataspace_id);
            if (status < 0) {
                goto out;
            }
            status = H5Dclose(dataset_id);
            if (status < 0) {
                goto out;
            }
        }
    }
    ret = tree_sequence_check_hdf5_dimensions(self, file_id);
    if (ret != 0) {
        goto out;
    }
    ret = 0;
out:
    return ret;
}

static int
tree_sequence_read_hdf5_data(tree_sequence_t *self, hid_t file_id)
{
    herr_t status;
    int ret = MSP_ERR_HDF5;
    hid_t dataset_id;
    struct _hdf5_field_read {
        const char *name;
        hid_t type;
        int empty;
        void *dest;
    };
    struct _hdf5_field_read fields[] = {
        {"/trees/left", H5T_NATIVE_UINT32, 0, NULL},
        {"/trees/right", H5T_NATIVE_UINT32, 0, NULL},
        {"/trees/node", H5T_NATIVE_UINT32, 0, NULL},
        {"/trees/children", H5T_NATIVE_UINT32, 0, NULL},
        {"/trees/time", H5T_NATIVE_DOUBLE, 0, NULL},
        {"/mutations/node", H5T_NATIVE_UINT32, 0, NULL},
        {"/mutations/position", H5T_NATIVE_DOUBLE, 0, NULL},
    };
    size_t num_fields = sizeof(fields) / sizeof(struct _hdf5_field_read);
    size_t j;

    fields[0].dest = self->trees.left;
    fields[1].dest = self->trees.right;
    fields[2].dest = self->trees.node;
    fields[3].dest = self->trees.children;
    fields[4].dest = self->trees.time;
    fields[5].dest = self->mutations.node;
    fields[6].dest = self->mutations.position;
    if (self->num_mutations == 0) {
        fields[5].empty = 1;
        fields[6].empty = 1;
    }
    for (j = 0; j < num_fields; j++) {
        if (!fields[j].empty) {
            dataset_id = H5Dopen(file_id, fields[j].name, H5P_DEFAULT);
            if (dataset_id < 0) {
                goto out;
            }
            status = H5Dread(dataset_id, fields[j].type, H5S_ALL,
                    H5S_ALL, H5P_DEFAULT, fields[j].dest);
            if (status < 0) {
                goto out;
            }
            status = H5Dclose(dataset_id);
            if (status < 0) {
                goto out;
            }
        }
    }
    ret = 0;
out:
    return ret;
}

static int
tree_sequence_read_hdf5_provenance(tree_sequence_t *self, hid_t file_id)
{
    int ret = MSP_ERR_HDF5;
    hid_t attr_id, atype, type_class, atype_mem;
    herr_t status;
    size_t size;
    struct _hdf5_string_read {
        const char *prefix;
        const char *name;
        char **dest;
        int included;
    };
    struct _hdf5_string_read fields[] = {
        {"trees", "environment", NULL, 1},
        {"trees", "parameters", NULL, 1},
        {"mutations", "environment", NULL, 0},
        {"mutations", "parameters", NULL, 0},
    };
    size_t num_fields = sizeof(fields) / sizeof(struct _hdf5_string_read);
    size_t j;
    char *str;

    fields[0].dest = &self->trees.environment;
    fields[1].dest = &self->trees.parameters;
    if (self->num_mutations > 0) {
        fields[2].included = 1;
        fields[3].included = 1;
        fields[2].dest = &self->mutations.environment;
        fields[3].dest = &self->mutations.parameters;
    }

    for (j = 0; j < num_fields; j++) {
        if (fields[j].included) {
            attr_id = H5Aopen_by_name(file_id, fields[j].prefix, fields[j].name,
                    H5P_DEFAULT, H5P_DEFAULT);
            if (attr_id < 0) {
                goto out;
            }
            atype = H5Aget_type(attr_id);
            if (atype < 0) {
                goto out;
            }
            type_class = H5Tget_class(atype);
            if (type_class < 0) {
                goto out;
            }
            if (type_class != H5T_STRING) {
                ret = MSP_ERR_FILE_FORMAT;
                goto out;
            }
            atype_mem = H5Tget_native_type(atype, H5T_DIR_ASCEND);
            if (atype_mem < 0) {
                goto out;
            }
            size = H5Tget_size(atype_mem);
            str = malloc(size + 1);
            if (str == NULL) {
                ret = MSP_ERR_NO_MEMORY;
                goto out;
            }
            status = H5Aread(attr_id, atype_mem, str);
            if (status < 0) {
                goto out;
            }
            str[size] = '\0';
            *fields[j].dest = str;
            status = H5Tclose(atype);
            if (status < 0) {
                goto out;
            }
            status = H5Tclose(atype_mem);
            if (status < 0) {
                goto out;
            }
            status = H5Aclose(attr_id);
            if (status < 0) {
                goto out;
            }
        }
    }
    ret = 0;
out:
    return ret;
}

int
tree_sequence_load(tree_sequence_t *self, const char *filename, int flags)
{
    int ret = MSP_ERR_GENERIC;
    herr_t status;
    hid_t file_id;

    memset(self, 0, sizeof(tree_sequence_t));
    file_id = H5Fopen(filename, H5F_ACC_RDONLY, H5P_DEFAULT);
    if (file_id < 0) {
        ret = MSP_ERR_HDF5;
        goto out;
    }
    status = tree_sequence_read_hdf5_metadata(self, file_id);
    if (status < 0) {
        goto out;
    }
    ret = tree_sequence_read_hdf5_dimensions(self, file_id);
    if (ret != 0) {
        goto out;
    }
    ret = tree_sequence_alloc(self);
    if (ret != 0) {
        goto out;
    }
    ret = tree_sequence_read_hdf5_data(self, file_id);
    if (ret != 0) {
        goto out;
    }
    status = tree_sequence_read_hdf5_provenance(self, file_id);
    if (status < 0) {
        ret = MSP_ERR_HDF5;
        goto out;
    }
    status = H5Fclose(file_id);
    if (status < 0) {
        ret = MSP_ERR_HDF5;
        goto out;
    }
    if (!(flags & MSP_SKIP_H5CLOSE)) {
        status = H5close();
        if (status < 0) {
            goto out;
        }
    }
    ret = tree_sequence_make_indexes(self);
    if (ret != 0) {
        goto out;
    }
    ret = 0;
out:
    return ret;
}

static int
tree_sequence_write_hdf5_data(tree_sequence_t *self, hid_t file_id, int flags)
{
    herr_t ret = -1;
    herr_t status;
    hid_t group_id, dataset_id, dataspace_id, plist_id;
    hsize_t dims[2];
    struct _hdf5_field_write {
        const char *name;
        hid_t storage_type;
        hid_t memory_type;
        int dimensions;
        size_t size;
        void *source;
    };
    struct _hdf5_field_write fields[] = {
        {"/trees/left", H5T_STD_U32LE, H5T_NATIVE_UINT32, 1, 0, NULL},
        {"/trees/right", H5T_STD_U32LE, H5T_NATIVE_UINT32, 1, 0, NULL},
        {"/trees/node", H5T_STD_U32LE, H5T_NATIVE_UINT32, 1, 0, NULL},
        {"/trees/children", H5T_STD_U32LE, H5T_NATIVE_UINT32, 2, 0, NULL},
        {"/trees/time", H5T_IEEE_F64LE, H5T_NATIVE_DOUBLE, 1, 0, NULL},
        {"/mutations/node", H5T_STD_U32LE, H5T_NATIVE_UINT32, 1, 0, NULL},
        {"/mutations/position", H5T_IEEE_F64LE, H5T_NATIVE_DOUBLE, 1, 0, NULL},
    };
    size_t num_fields = sizeof(fields) / sizeof(struct _hdf5_field_write);
    struct _hdf5_group_write {
        const char *name;
        int included;
    };
    struct _hdf5_group_write groups[] = {
        {"/trees", 1},
        {"/mutations", 1},
    };
    size_t num_groups = sizeof(groups) / sizeof(struct _hdf5_group_write);
    size_t j;

    fields[0].source = self->trees.left;
    fields[1].source = self->trees.right;
    fields[2].source = self->trees.node;
    fields[3].source = self->trees.children;
    fields[4].source = self->trees.time;
    fields[5].source = self->mutations.node;
    fields[6].source = self->mutations.position;
    for (j = 0; j < 5; j++) {
        fields[j].size = self->num_records;
    }
    for (j = 5; j < 7; j++) {
        fields[j].size = self->num_mutations;
    }
    if (self->num_mutations == 0) {
        groups[1].included = 0;
    }
    /* Create the groups */
    for (j = 0; j < num_groups; j++) {
        if (groups[j].included) {
            group_id = H5Gcreate(file_id, groups[j].name, H5P_DEFAULT, H5P_DEFAULT,
                    H5P_DEFAULT);
            if (group_id < 0) {
                goto out;
            }
            status = H5Gclose(group_id);
            if (status < 0) {
                goto out;
            }
        }
    }
    /* now write the datasets */
    for (j = 0; j < num_fields; j++) {
        if (fields[j].size > 0) {
            dims[0] = fields[j].size;
            dims[1] = 2; /* unused except for children */
            dataspace_id = H5Screate_simple(fields[j].dimensions, dims, NULL);
            if (dataspace_id < 0) {
                goto out;
            }
            plist_id = H5Pcreate(H5P_DATASET_CREATE);
            if (plist_id < 0) {
                goto out;
            }
            /* Set the chunk size to the full size of the dataset since we
             * always read the full thing.
             */
            status = H5Pset_chunk(plist_id, fields[j].dimensions, dims);
            if (status < 0) {
                goto out;
            }
            if (flags & MSP_ZLIB_COMPRESSION) {
                /* Turn on byte shuffling to improve compression */
                status = H5Pset_shuffle(plist_id);
                if (status < 0) {
                    goto out;
                }
                /* Set zlib compression at level 9 (best compression) */
                status = H5Pset_deflate(plist_id, 9);
                if (status < 0) {
                    goto out;
                }
            }
            /* Turn on Fletcher32 checksums for integrity checks */
            status = H5Pset_fletcher32(plist_id);
            if (status < 0) {
                goto out;
            }
            dataset_id = H5Dcreate2(file_id, fields[j].name,
                    fields[j].storage_type, dataspace_id, H5P_DEFAULT,
                    plist_id, H5P_DEFAULT);
            if (dataset_id < 0) {
                goto out;
            }
            if (fields[j].size > 0) {
                /* Don't write zero sized datasets to work-around problems
                 * with older versions of hdf5. */
                status = H5Dwrite(dataset_id, fields[j].memory_type, H5S_ALL,
                        H5S_ALL, H5P_DEFAULT, fields[j].source);
                if (status < 0) {
                    goto out;
                }
            }
            status = H5Dclose(dataset_id);
            if (status < 0) {
                goto out;
            }
            status = H5Pclose(plist_id);
            if (status < 0) {
                goto out;
            }
        }
    }
    ret = 0;
out:
    return ret;
}

static int
tree_sequence_write_hdf5_provenance(tree_sequence_t *self, hid_t file_id)
{
    herr_t ret = -1;
    herr_t status;
    hid_t group_id, dataspace_id, attr_id, type_id;
    struct _hdf5_string_write {
        const char *group;
        const char *name;
        const char *value;
        int included;
    };
    struct _hdf5_string_write fields[] = {
        {"trees", "environment", NULL, 1},
        {"trees", "parameters", NULL, 1},
        {"mutations", "environment", NULL, 0},
        {"mutations", "parameters", NULL, 0},
    };
    size_t num_fields = sizeof(fields) / sizeof(struct _hdf5_string_write);
    size_t j;

    fields[0].value = self->trees.environment;
    fields[1].value = self->trees.parameters;
    if (self->num_mutations > 0) {
        fields[2].included = 1;
        fields[2].value = self->mutations.environment;
        fields[3].included = 1;
        fields[3].value = self->mutations.parameters;
    }
    for (j = 0; j < num_fields; j++) {
        if (fields[j].included) {
            assert(fields[j].value != NULL);
            group_id = H5Gopen(file_id, fields[j].group, H5P_DEFAULT);
            if (group_id < 0) {
                goto out;
            }
            dataspace_id = H5Screate(H5S_SCALAR);
            if (dataspace_id < 0) {
                goto out;
            }
            type_id = H5Tcopy(H5T_C_S1);
            if (type_id < 0) {
                goto out;
            }
            status = H5Tset_size(type_id, strlen(fields[j].value));
            if (status < 0) {
                goto out;
            }
            attr_id = H5Acreate(group_id, fields[j].name, type_id,
                    dataspace_id, H5P_DEFAULT, H5P_DEFAULT);
            if (attr_id < 0) {
                goto out;
            }
            status = H5Awrite(attr_id, type_id, fields[j].value);
            if (status < 0) {
                goto out;
            }
            status = H5Aclose(attr_id);
            if (status < 0) {
                goto out;
            }
            status = H5Tclose(type_id);
            if (status < 0) {
                goto out;
            }
            status = H5Sclose(dataspace_id);
            if (status < 0) {
                goto out;
            }
            status = H5Gclose(group_id);
            if (status < 0) {
                goto out;
            }
        }
    }
    ret = 0;
out:
    return ret;
}


static int
tree_sequence_write_hdf5_metadata(tree_sequence_t *self, hid_t file_id)
{
    herr_t status = -1;
    hid_t attr_id, dataspace_id;
    hsize_t dims = 1;
    uint32_t version[2] = {
        MSP_FILE_FORMAT_VERSION_MAJOR, MSP_FILE_FORMAT_VERSION_MINOR};

    struct _hdf5_metadata_write {
        const char *name;
        hid_t parent;
        hid_t storage_type;
        hid_t memory_type;
        size_t size;
        void *source;
    };
    struct _hdf5_metadata_write fields[] = {
        {"format_version", 0, H5T_STD_U32LE, H5T_NATIVE_UINT32, 2, NULL},
        {"sample_size", 0, H5T_STD_U32LE, H5T_NATIVE_UINT32, 0, NULL},
        {"num_loci", 0, H5T_STD_U32LE, H5T_NATIVE_UINT32, 0, NULL},
    };
    /* TODO random_seed, population_models, etc. */
    size_t num_fields = sizeof(fields) / sizeof(struct _hdf5_metadata_write);
    size_t j;

    fields[0].source = version;
    fields[1].source = &self->sample_size;
    fields[2].source = &self->num_loci;

    for (j = 0; j < num_fields; j++) {
        if (fields[j].size == 0) {
            dataspace_id = H5Screate(H5S_SCALAR);
        } else {
            dims = fields[j].size;
            dataspace_id = H5Screate_simple(1, &dims, NULL);
        }
        if (dataspace_id < 0) {
            status = dataspace_id;
            goto out;
        }
        attr_id = H5Acreate(file_id, fields[j].name,
                fields[j].storage_type, dataspace_id, H5P_DEFAULT,
                H5P_DEFAULT);
        if (attr_id < 0) {
            goto out;
        }
        status = H5Awrite(attr_id, fields[j].memory_type, fields[j].source);
        if (status < 0) {
            goto out;
        }
        status = H5Aclose(attr_id);
        if (status < 0) {
            goto out;
        }
        status = H5Sclose(dataspace_id);
        if (status < 0) {
            goto out;
        }
    }
 out:
    return status;
}

int
tree_sequence_dump(tree_sequence_t *self, const char *filename, int flags)
{
    int ret = MSP_ERR_HDF5;
    herr_t status;
    hid_t file_id;

    file_id = H5Fcreate(filename, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    if (file_id < 0) {
        goto out;
    }
    status = tree_sequence_write_hdf5_metadata(self, file_id);
    if (status < 0) {
        goto out;
    }
    status = tree_sequence_write_hdf5_data(self, file_id, flags);
    if (status < 0) {
        goto out;
    }
    status = tree_sequence_write_hdf5_provenance(self, file_id);
    if (status < 0) {
        goto out;
    }
    status = H5Fclose(file_id);
    if (status < 0) {
        goto out;
    }
    if (!(flags & MSP_SKIP_H5CLOSE)) {
        status = H5close();
        if (status < 0) {
            goto out;
        }
    }
    ret = 0;
out:
    return ret;
}

uint32_t
tree_sequence_get_num_loci(tree_sequence_t *self)
{
    return self->num_loci;
}

uint32_t
tree_sequence_get_sample_size(tree_sequence_t *self)
{
    return self->sample_size;
}

uint32_t
tree_sequence_get_num_nodes(tree_sequence_t *self)
{
    return self->num_nodes;
}

size_t
tree_sequence_get_num_coalescence_records(tree_sequence_t *self)
{
    return self->num_records;
}

size_t
tree_sequence_get_num_mutations(tree_sequence_t *self)
{
    return self->num_mutations;
}

/* Returns the parameters for the trees encoded as JSON. This string
 * should NOT be freed by client code.
 */
char *
tree_sequence_get_simulation_parameters(tree_sequence_t *self)
{
    return self->trees.parameters;
}

/* Returns the parameters for the mutations encoded as JSON. This string
 * should NOT be freed by client code. This is NULL if mutations have
 * not been generated.
 */
char *
tree_sequence_get_mutation_parameters(tree_sequence_t *self)
{
    return self->mutations.parameters;
}

int
tree_sequence_get_record(tree_sequence_t *self, size_t index,
        coalescence_record_t *record, int order)
{
    int ret = 0;
    size_t j;

    if (index >= self->num_records) {
        ret = MSP_ERR_OUT_OF_BOUNDS;
        goto out;
    }
    switch (order) {
        case MSP_ORDER_TIME:
            j = index;
            break;
        case MSP_ORDER_LEFT:
            j = self->trees.insertion_order[index];
            break;
        case MSP_ORDER_RIGHT:
            j = self->trees.removal_order[index];
            break;
        default:
            ret = MSP_ERR_BAD_ORDERING;
            goto out;
    }
    record->left = self->trees.left[j];
    record->right = self->trees.right[j];
    record->node = self->trees.node[j];
    record->children[0] = self->trees.children[2 * j];
    record->children[1] = self->trees.children[2 * j + 1];
    record->time = self->trees.time[j];
out:
    return ret;
}

int
tree_sequence_get_mutations(tree_sequence_t *self, mutation_t *mutations)
{
    int ret = 0;
    size_t j;

    assert(mutations != NULL);
    for (j = 0; j < self->num_mutations; j++) {
        mutations[j].position = self->mutations.position[j];
        mutations[j].node = self->mutations.node[j];
    }
    return ret;
}

/*
 * This is a convenience short cut for sparse tree alloc in the common
 * case where we're allocating it for a given sequence.
 */
int
tree_sequence_alloc_sparse_tree(tree_sequence_t *self, sparse_tree_t *tree,
        uint32_t *tracked_leaves, uint32_t num_tracked_leaves, int flags)
{
    return sparse_tree_alloc(tree, self->sample_size, self->num_nodes,
            self->num_mutations, tracked_leaves, num_tracked_leaves, flags);
}

int
tree_sequence_set_mutations(tree_sequence_t *self, size_t num_mutations,
        mutation_t *mutations)
{
    int ret = -1;
    size_t j;
    mutation_t **mutation_ptrs = NULL;

    if (self->num_mutations > 0) {
        /* any mutations that were there previously are overwritten. */
        if (self->mutations.node != NULL) {
            free(self->mutations.node);
        }
        if (self->mutations.position != NULL) {
            free(self->mutations.position);
        }
        if (self->mutations.parameters != NULL) {
            free(self->mutations.parameters);
        }
        if (self->mutations.environment != NULL) {
            free(self->mutations.environment);
        }
    }
    self->num_mutations = 0;
    self->mutations.position = NULL;
    self->mutations.node = NULL;
    self->mutations.parameters = NULL;
    self->mutations.environment = NULL;
    if (num_mutations > 0) {
        /* Allocate the storage we need to keep the mutations. */
        mutation_ptrs = malloc(num_mutations * sizeof(mutation_t *));
        self->mutations.node = malloc(num_mutations * sizeof(uint32_t));
        self->mutations.position = malloc(num_mutations * sizeof(double));
        if (mutation_ptrs == NULL || self->mutations.node == NULL
                || self->mutations.position == NULL) {
            ret = MSP_ERR_NO_MEMORY;
            goto out;
        }
        for (j = 0; j < num_mutations; j++) {
            mutation_ptrs[j] = mutations + j;
            if (mutations[j].position < 0
                    || mutations[j].position > self->num_loci
                    || mutations[j].node == 0
                    || mutations[j].node > self->num_nodes) {
                ret = MSP_ERR_BAD_MUTATION;
                goto out;
            }
        }
        /* Mutations are required to be sorted in position order. */
        qsort(mutation_ptrs, num_mutations, sizeof(mutation_t *),
                cmp_mutation_pointer);
        self->num_mutations = num_mutations;
        for (j = 0; j < num_mutations; j++) {
            self->mutations.node[j] = mutation_ptrs[j]->node;
            self->mutations.position[j] = mutation_ptrs[j]->position;
        }
    }
    ret = 0;
out:
    if (mutation_ptrs != NULL) {
        free(mutation_ptrs);
    }
    return ret;
}

/* TODO mutations generation should be spun out into a separate class.
 * We should really just do this in Python and send the resulting
 * mutation objects here for storage. It's not an expensive operation.
 */
int
tree_sequence_generate_mutations(tree_sequence_t *self, double mutation_rate,
        unsigned long random_seed)
{
    int ret = -1;
    coalescence_record_t cr;
    uint32_t j, k, l, child;
    gsl_rng *rng = NULL;
    double *times = NULL;
    mutation_t *mutations = NULL;
    unsigned int branch_mutations;
    size_t num_mutations;
    double branch_length, distance, mu, position;
    size_t buffer_size;
    size_t block_size = 2 << 10; /* alloc in blocks of 1M */
    void *p;

    buffer_size = block_size;
    num_mutations = 0;
    rng = gsl_rng_alloc(gsl_rng_default);
    if (rng == NULL) {
        goto out;
    }
    gsl_rng_set(rng, random_seed);
    times = calloc(self->num_nodes + 1, sizeof(double));
    mutations = malloc(buffer_size * sizeof(mutation_t));
    if (times == NULL || mutations == NULL) {
        ret = MSP_ERR_NO_MEMORY;
        goto out;
    }
    for (j = 0; j < self->num_records; j++) {
        ret = tree_sequence_get_record(self, j, &cr, MSP_ORDER_TIME);
        if (ret != 0) {
            goto out;
        }
        times[cr.node] = cr.time;
        distance = cr.right - cr.left;
        for (k = 0; k < 2; k++) {
            child = cr.children[k];
            branch_length = cr.time - times[child];
            mu = branch_length * distance * mutation_rate;
            branch_mutations = gsl_ran_poisson(rng, mu);
            for (l = 0; l < branch_mutations; l++) {
                position = gsl_ran_flat(rng, cr.left, cr.right);
                if (num_mutations >= buffer_size) {
                    buffer_size += block_size;
                    p = realloc(mutations, buffer_size * sizeof(mutation_t));
                    if (p == NULL) {
                        ret = MSP_ERR_NO_MEMORY;
                        goto out;
                    }
                    mutations = p;
                }
                mutations[num_mutations].node = child;
                mutations[num_mutations].position = position;
                num_mutations++;
            }
        }
    }
    ret = tree_sequence_set_mutations(self, num_mutations, mutations);
    if (ret != 0) {
        goto out;
    }
    if (num_mutations > 0) {
        /* TODO Make a generic method to do this attached to the
         * mutation_generator class, and add generic string setter
         * methods for the parameters and environment in the
         * tree_sequence class. This will allow us to set the provenance
         * information from anywhere.
         */
        ret = encode_mutation_parameters(mutation_rate, random_seed,
                &self->mutations.parameters);
        if (ret != 0) {
            goto out;
        }
        ret = encode_environment(&self->mutations.environment);
        if (ret != 0) {
            goto out;
        }
    }
out:
    if (times != NULL) {
        free(times);
    }
    if (mutations != NULL) {
        free(mutations);
    }
    if (rng != NULL) {
        gsl_rng_free(rng);
    }
    return ret;
}


/* ======================================================== *
 * Tree diff iterator.
 * ======================================================== */


int
tree_diff_iterator_alloc(tree_diff_iterator_t *self,
        tree_sequence_t *tree_sequence)
{
    int ret = 0;

    assert(tree_sequence != NULL);
    memset(self, 0, sizeof(tree_diff_iterator_t));
    self->sample_size = tree_sequence_get_sample_size(tree_sequence);
    self->num_nodes = tree_sequence_get_num_nodes(tree_sequence);
    self->num_records = tree_sequence_get_num_coalescence_records(
            tree_sequence);
    self->tree_sequence = tree_sequence;
    self->insertion_index = 0;
    self->removal_index = 0;
    self->tree_left = 0;
    /* The maximum number of records is to remove and insert all n - 1
     * records */
    self->node_records = malloc(2 * self->sample_size * sizeof(node_record_t));
    if (self->node_records == NULL) {
        ret = MSP_ERR_NO_MEMORY;
        goto out;
    }
out:
    return ret;
}

int
tree_diff_iterator_free(tree_diff_iterator_t *self)
{
    int ret = 0;
    if (self->node_records != NULL) {
        free(self->node_records);
    }
    return ret;
}

void
tree_diff_iterator_print_state(tree_diff_iterator_t *self)
{
    printf("tree_diff_iterator state\n");
    printf("num_records = %d\n", (int) self->num_records);
    printf("insertion_index = %d\n", (int) self->insertion_index);
    printf("removal_index = %d\n", (int) self->removal_index);
    printf("tree_left = %d\n", (int) self->tree_left);
}

int
tree_diff_iterator_next(tree_diff_iterator_t *self, uint32_t *length,
        node_record_t **nodes_out, node_record_t **nodes_in)
{
    int ret = 0;
    uint32_t k;
    uint32_t last_left = self->tree_left;
    size_t next_node_record = 0;
    tree_sequence_t *s = self->tree_sequence;
    node_record_t *out_head = NULL;
    node_record_t *out_tail = NULL;
    node_record_t *in_head = NULL;
    node_record_t *in_tail = NULL;
    node_record_t *w = NULL;

    assert(s != NULL);
    if (self->insertion_index < self->num_records) {
        /* First we remove the stale records */
        while (s->trees.right[s->trees.removal_order[self->removal_index]]
                == self->tree_left) {
            k = s->trees.removal_order[self->removal_index];
            assert(next_node_record < 2 * self->sample_size);
            w = &self->node_records[next_node_record];
            next_node_record++;
            w->time = s->trees.time[k];
            w->node = s->trees.node[k];
            w->children[0] = s->trees.children[2 * k];
            w->children[1] = s->trees.children[2 * k + 1];
            w->next = NULL;
            if (out_head == NULL) {
                out_head = w;
                out_tail = w;
            } else {
                out_tail->next = w;
                out_tail = w;
            }
            self->removal_index++;
        }
        /* Now insert the new records */
        while (self->insertion_index < self->num_records &&
                s->trees.left[s->trees.insertion_order[self->insertion_index]]
                == self->tree_left) {
            k = s->trees.insertion_order[self->insertion_index];
            assert(next_node_record < 2 * self->sample_size);
            w = &self->node_records[next_node_record];
            next_node_record++;
            w->time = s->trees.time[k];
            w->node = s->trees.node[k];
            w->children[0] = s->trees.children[2 * k];
            w->children[1] = s->trees.children[2 * k + 1];
            w->next = NULL;
            if (in_head == NULL) {
                in_head = w;
                in_tail = w;
            } else {
                in_tail->next = w;
                in_tail = w;
            }
            self->insertion_index++;
        }
        /* Update the left coordinate */
        self->tree_left = s->trees.right[s->trees.removal_order[
            self->removal_index]];
        ret = 1;
    }
    *nodes_out = out_head;
    *nodes_in = in_head;
    *length = self->tree_left - last_left;
    return ret;
}

/* ======================================================== *
 * sparse tree
 * ======================================================== */

int
sparse_tree_alloc(sparse_tree_t *self, uint32_t sample_size, uint32_t num_nodes,
        size_t max_mutations, uint32_t *tracked_leaves,
        uint32_t num_tracked_leaves, int flags)
{
    int ret = MSP_ERR_NO_MEMORY;
    uint32_t j, u;

    memset(self, 0, sizeof(sparse_tree_t));
    if (num_nodes == 0 || sample_size == 0) {
        ret = MSP_ERR_BAD_PARAM_VALUE;
        goto out;
    }
    self->num_nodes = num_nodes;
    self->sample_size = sample_size;
    self->flags = flags;
    self->parent = malloc((self->num_nodes + 1) * sizeof(uint32_t));
    self->time = malloc((self->num_nodes + 1) * sizeof(double));
    self->children = malloc(2 * (self->num_nodes + 1) * sizeof(uint32_t));
    if (self->time == NULL || self->parent == NULL || self->children == NULL) {
        goto out;
    }
    /* the maximum possible height of the tree is n + 1, including
     * the null value. */
    self->stack1 = malloc((self->sample_size + 1) * sizeof(uint32_t));
    self->stack2 = malloc((self->sample_size + 1) * sizeof(uint32_t));
    if (self->stack1 == NULL || self->stack2 == NULL) {
        goto out;
    }
    self->max_mutations = max_mutations;
    self->num_mutations = 0;
    self->mutations = malloc(max_mutations * sizeof(mutation_t));
    if (self->mutations == NULL) {
        goto out;
    }
    if (self->flags & MSP_COUNT_LEAVES) {
        self->num_leaves = malloc((self->num_nodes + 1) * sizeof(uint32_t));
        self->num_tracked_leaves = malloc((self->num_nodes + 1)
                * sizeof(uint32_t));
        if (self->num_leaves == NULL || self->num_tracked_leaves == NULL) {
            goto out;
        }
        self->num_leaves[0] = 0;
        self->num_tracked_leaves[0] = 0;
        for (j = 1; j <= self->sample_size; j++) {
            self->num_leaves[j] = 1;
            self->num_tracked_leaves[j] = 0;
        }
        for (j = 0; j < num_tracked_leaves; j++) {
            u = tracked_leaves[j];
            if (u == 0 || u > self->sample_size) {
                ret = MSP_ERR_BAD_PARAM_VALUE;
                goto out;
            }
            self->num_tracked_leaves[u] = 1;
        }
    }
    ret = 0;
out:
    return ret;
}

int
sparse_tree_free(sparse_tree_t *self)
{
    if (self->parent != NULL) {
        free(self->parent);
    }
    if (self->time != NULL) {
        free(self->time);
    }
    if (self->children != NULL) {
        free(self->children);
    }
    if (self->stack1 != NULL) {
        free(self->stack1);
    }
    if (self->stack2 != NULL) {
        free(self->stack2);
    }
    if (self->mutations != NULL) {
        free(self->mutations);
    }
    if (self->num_leaves != NULL) {
        free(self->num_leaves);
    }
    if (self->num_tracked_leaves != NULL) {
        free(self->num_tracked_leaves);
    }
    return 0;
}

int
sparse_tree_clear(sparse_tree_t *self)
{
    int ret = 0;
    size_t N = self->num_nodes + 1;
    size_t n = self->sample_size;

    self->left = 0;
    self->right = 0;
    self->root = 0;
    memset(self->parent, 0, N * sizeof(uint32_t));
    memset(self->time, 0, N * sizeof(double));
    memset(self->children, 0, 2 * N * sizeof(uint32_t));
    if (self->flags & MSP_COUNT_LEAVES) {
        memset(self->num_leaves + n + 1, 0, (N - n - 1) * sizeof(uint32_t));
        memset(self->num_tracked_leaves + n + 1, 0,
                (N - n - 1) * sizeof(uint32_t));
    }
    return ret;
}

int
sparse_tree_get_mrca(sparse_tree_t *self, uint32_t u, uint32_t v,
        uint32_t *mrca)
{
    int ret = 0;
    uint32_t w = 0;
    uint32_t *s1 = self->stack1;
    uint32_t *s2 = self->stack2;
    uint32_t j;
    int l1, l2;

    if (u == 0 || v == 0 || u > self->num_nodes || v > self->num_nodes) {
        ret = MSP_ERR_BAD_PARAM_VALUE;
        goto out;
    }
    j = u;
    l1 = 0;
    while (j != 0) {
        assert(l1 < (int) self->sample_size);
        s1[l1] = j;
        l1++;
        j = self->parent[j];
    }
    s1[l1] = 0;
    j = v;
    l2 = 0;
    while (j != 0) {
        assert(l2 < (int) self->sample_size);
        s2[l2] = j;
        l2++;
        j = self->parent[j];
    }
    s2[l2] = 0;
    do {
        w = s1[l1];
        l1--;
        l2--;
    } while (l1 >= 0 && l2 >= 0 && s1[l1] == s2[l2]);
    *mrca = w;
    ret = 0;
out:
    return ret;
}

static int
sparse_tree_get_num_leaves_by_traversal(sparse_tree_t *self, uint32_t u,
        uint32_t *num_leaves)
{
    int ret = 0;
    uint32_t *stack = self->stack1;
    uint32_t v, c;
    uint32_t count = 0;
    int stack_top = 0;

    stack[0] = u;
    while (stack_top >= 0) {
        v = stack[stack_top];
        stack_top--;
        if (1 <= v && v <= self->sample_size) {
            count++;
        } else if (self->children[2 * v] != 0) {
            for (c = 0; c < 2; c++) {
                stack_top++;
                stack[stack_top] = self->children[2 * v + c];
            }
        }
    }
    *num_leaves = count;
    return ret;
}


int
sparse_tree_get_num_leaves(sparse_tree_t *self, uint32_t u,
        uint32_t *num_leaves)
{
    int ret = 0;

    if (self->flags & MSP_COUNT_LEAVES) {
        *num_leaves = self->num_leaves[u];
    } else {
        ret = sparse_tree_get_num_leaves_by_traversal(self, u, num_leaves);
    }
    return ret;
}

int
sparse_tree_get_num_tracked_leaves(sparse_tree_t *self, uint32_t u,
        uint32_t *num_tracked_leaves)
{
    int ret = 0;

    if (! (self->flags & MSP_COUNT_LEAVES)) {
        ret = MSP_ERR_UNSUPPORTED_OPERATION;
        goto out;
    }
    *num_tracked_leaves = self->num_tracked_leaves[u];
out:
    return ret;
}


/* ======================================================== *
 * sparse tree iterator
 * ======================================================== */

int
sparse_tree_iterator_alloc(sparse_tree_iterator_t *self,
        tree_sequence_t *tree_sequence, sparse_tree_t *tree)
{
    int ret = MSP_ERR_NO_MEMORY;

    assert(tree_sequence != NULL);
    assert(tree != NULL);
    assert(tree->time != NULL && tree->parent != NULL
            && tree->children != NULL);
    if (tree_sequence_get_num_nodes(tree_sequence) != tree->num_nodes ||
            tree_sequence_get_sample_size(tree_sequence)
                != tree->sample_size ||
            tree_sequence_get_num_mutations(tree_sequence)
                != tree->max_mutations) {
        ret = MSP_ERR_BAD_PARAM_VALUE;
        goto out;
    }
    memset(self, 0, sizeof(sparse_tree_iterator_t));
    self->sample_size = tree_sequence_get_sample_size(tree_sequence);
    self->num_nodes = tree_sequence_get_num_nodes(tree_sequence);
    self->num_records = tree_sequence_get_num_coalescence_records(
            tree_sequence);
    self->tree_sequence = tree_sequence;
    self->tree = tree;
    self->tree->sample_size = self->sample_size;
    self->insertion_index = 0;
    self->removal_index = 0;
    self->mutation_index = 0;
    ret = sparse_tree_clear(self->tree);
out:
    return ret;
}

int
sparse_tree_iterator_free(sparse_tree_iterator_t *self)
{
    int ret = 0;
    return ret;
}

static void
sparse_tree_iterator_check_state(sparse_tree_iterator_t *self)
{
    uint32_t u, v, j, num_leaves;
    int err;

    assert(self->tree->num_nodes == self->num_nodes);
    for (j = 1; j < self->sample_size + 1; j++) {
        u = j;
        assert(self->tree->time[u] == 0.0);
        assert(self->tree->children[2 * j] == 0);
        assert(self->tree->children[2 * j + 1] == 0);
        while (self->tree->parent[u] != 0) {
            v = self->tree->parent[u];
            assert(self->tree->children[2 * v] == u
                    || self->tree->children[2 * v + 1] == u);
            u = v;
            assert(self->tree->time[u] > 0.0);
        }
        assert(u == self->tree->root);
    }
    if (self->tree->flags & MSP_COUNT_LEAVES) {
        for (j = 1; j <= self->num_nodes; j++) {
            err = sparse_tree_get_num_leaves_by_traversal(self->tree, j,
                    &num_leaves);
            assert(err == 0);
            assert(num_leaves == self->tree->num_leaves[j]);
        }
    }
}

void
sparse_tree_iterator_print_state(sparse_tree_iterator_t *self)
{
    size_t j;

    printf("sparse_tree_iterator state\n");
    printf("insertion_index = %d\n", (int) self->insertion_index);
    printf("removal_index = %d\n", (int) self->removal_index);
    printf("mutation_index = %d\n", (int) self->mutation_index);
    printf("num_records = %d\n", (int) self->num_records);
    printf("tree.flags = %d\n", self->tree->flags);
    printf("tree.left = %d\n", self->tree->left);
    printf("tree.right = %d\n", self->tree->right);
    printf("tree.root = %d\n", self->tree->root);
    for (j = 0; j < self->tree->num_nodes + 1; j++) {
        printf("\t%d\t%d\t%d\t%d\t%f", (int) j, self->tree->parent[j],
                self->tree->children[2 * j], self->tree->children[2 * j + 1],
                self->tree->time[j]);
        if (self->tree->flags & MSP_COUNT_LEAVES) {
            printf("\t%d\t%d", self->tree->num_leaves[j],
                    self->tree->num_tracked_leaves[j]);
        }
        printf("\n");
    }
    printf("mutations = \n");
    for (j = 0; j < self->tree->num_mutations; j++) {
        printf("\t%d @ %f\n", self->tree->mutations[j].node,
                self->tree->mutations[j].position);
    }
    sparse_tree_iterator_check_state(self);
}

int
sparse_tree_iterator_next(sparse_tree_iterator_t *self)
{
    int ret = 0;
    uint32_t j, k, u, v, c[2], all_leaves_diff, tracked_leaves_diff;
    tree_sequence_t *s = self->tree_sequence;
    sparse_tree_t *t = self->tree;

    assert(t != NULL && s != NULL);
    if (self->insertion_index < self->num_records) {
        /* First we remove the stale records */
        while (s->trees.right[s->trees.removal_order[self->removal_index]]
                == t->right) {
            k = s->trees.removal_order[self->removal_index];
            u = s->trees.node[k];
            c[0] = s->trees.children[2 * k];
            c[1] = s->trees.children[2 * k + 1];
            for (j = 0; j < 2; j++) {
                t->parent[c[j]] = 0;
                t->children[2 * u + j] = 0;
            }
            t->time[u] = 0;
            if (u == t->root) {
                t->root = GSL_MAX(c[0], c[1]);
            }
            self->removal_index++;
            if (t->flags & MSP_COUNT_LEAVES) {
                all_leaves_diff = t->num_leaves[u];
                tracked_leaves_diff = t->num_tracked_leaves[u];
                /* propogate this loss up as far as we can */
                v = u;
                while (v != 0) {
                    t->num_leaves[v] -= all_leaves_diff;
                    t->num_tracked_leaves[v] -= tracked_leaves_diff;
                    v = t->parent[v];
                }
            }
        }
        /* Update the interval */
        t->left = t->right;
        t->right = s->trees.right[s->trees.removal_order[self->removal_index]];
        /* Now insert the new records */
        while (self->insertion_index < self->num_records &&
                s->trees.left[s->trees.insertion_order[self->insertion_index]]
                == t->left) {
            k = s->trees.insertion_order[self->insertion_index];
            u = s->trees.node[k];
            c[0] = s->trees.children[2 * k];
            c[1] = s->trees.children[2 * k + 1];
            for (j = 0; j < 2; j++) {
                t->parent[c[j]] = u;
                t->children[2 * u + j] = c[j];
            }
            t->time[u] = s->trees.time[k];
            if (u >t->root) {
                t->root = u;
            }
            self->insertion_index++;
            if (t->flags & MSP_COUNT_LEAVES) {
                all_leaves_diff = t->num_leaves[c[0]] + t->num_leaves[c[1]];
                tracked_leaves_diff = t->num_tracked_leaves[c[0]]
                    + t->num_tracked_leaves[c[1]];
                /* propogate this gain up as far as we can */
                v = u;
                while (v != 0) {
                    t->num_leaves[v] += all_leaves_diff;
                    t->num_tracked_leaves[v] += tracked_leaves_diff;
                    v = t->parent[v];
                }
            }
        }
        /* In very rare situations, we have to traverse upwards to find the
         * new root.
         */
        while (t->parent[t->root] != 0) {
            t->root = t->parent[t->root];
        }
        ret = 1;
        /* now update the mutations */
        t->num_mutations = 0;
        while (self->mutation_index < s->num_mutations
                && s->mutations.position[self->mutation_index] < t->right) {
            assert(t->num_mutations < t->max_mutations);
            t->mutations[t->num_mutations].position =
                s->mutations.position[self->mutation_index];
            t->mutations[t->num_mutations].node =
                s->mutations.node[self->mutation_index];
            self->mutation_index++;
            t->num_mutations++;
        }
    }
    return ret;
}
