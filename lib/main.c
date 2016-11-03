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
#include <limits.h>
#include <stdarg.h>
#include <float.h>

#include <libconfig.h>

#include "msprime.h"
#include "err.h"

typedef struct {
    double mutation_rate;
    unsigned long random_seed;
} mutation_params_t;

static void
fatal_error(const char *msg, ...)
{
    va_list argp;
    fprintf(stderr, "main:");
    va_start(argp, msg);
    vfprintf(stderr, msg, argp);
    va_end(argp);
    fprintf(stderr, "\n");
    exit(EXIT_FAILURE);
}

static int
read_population_models(msp_t *msp, config_t *config)
{
    int ret = 0;
    int j;
    const char *type;
    double time, param;
    int num_population_models;
    config_setting_t *s, *t;
    config_setting_t *setting = config_lookup(config, "population_models");

    if (setting == NULL) {
        fatal_error("population_models is a required parameter");
    }
    if (config_setting_is_list(setting) == CONFIG_FALSE) {
        fatal_error("population_models must be a list");
    }
    num_population_models = config_setting_length(setting);
    for (j = 0; j < num_population_models; j++) {
        s = config_setting_get_elem(setting, (unsigned int) j);
        if (s == NULL) {
            fatal_error("error reading population_models[%d]", j);
        }
        if (config_setting_is_group(s) == CONFIG_FALSE) {
            fatal_error("population_models[%d] not a group", j);
        }
        t = config_setting_get_member(s, "time");
        if (t == NULL) {
            fatal_error("time not specified");
        }
        time = config_setting_get_float(t);
        if (time < 0.0) {
            fatal_error("population_model time must be > 0");
        }
        t = config_setting_get_member(s, "param");
        if (t == NULL) {
            fatal_error("param not specified");
        }
        param = config_setting_get_float(t);
        t = config_setting_get_member(s, "type");
        if (t == NULL) {
            fatal_error("type not specified");
        }
        type = config_setting_get_string(t);
        if (strcmp(type, "constant") == 0) {
            ret = msp_add_constant_population_model(msp, time, param);
        } else if (strcmp(type, "exponential") == 0) {
            ret = msp_add_exponential_population_model(msp, time, param);
        } else {
            fatal_error("unknown population_model type '%s'", type);
        }
        if (ret != 0) {
            goto out;
        }
    }
out:
    return ret;
}

static int
get_configuration(msp_t *msp, mutation_params_t *mutation_params,
        char **output_file, const char *filename)
{
    int ret = 0;
    int err;
    int int_tmp;
    double double_tmp;
    char *str;
    const char *str_tmp;
    config_t *config = malloc(sizeof(config_t));

    if (config == NULL) {
        fatal_error("no memory");
    }
    config_init(config);
    err = config_read_file(config, filename);
    if (err == CONFIG_FALSE) {
        fatal_error("configuration error:%s at line %d in file %s\n",
                config_error_text(config), config_error_line(config),
                filename);
    }
    if (config_lookup_int(config, "sample_size", &int_tmp) == CONFIG_FALSE) {
        fatal_error("sample_size is a required parameter");
    }
    ret = msp_alloc(msp, (uint32_t) int_tmp);
    if (config_lookup_int(config, "num_loci", &int_tmp) == CONFIG_FALSE) {
        fatal_error("num_loci is a required parameter");
    }
    msp_set_num_loci(msp, (uint32_t) int_tmp);
    if (config_lookup_int(config, "random_seed", &int_tmp) == CONFIG_FALSE) {
        fatal_error("random_seed is a required parameter");
    }
    msp_set_random_seed(msp, (unsigned long) int_tmp);
    /* Set the random seed for mutations to the same value */
    mutation_params->random_seed = (unsigned long) int_tmp;
    if (config_lookup_float(config, "recombination_rate", &double_tmp)
            == CONFIG_FALSE) {
        fatal_error("recombination_rate is a required parameter");
    }
    msp_set_scaled_recombination_rate(msp, double_tmp);

    /* Set the mutation rate */
    if (config_lookup_float(config,
            "mutation_rate", &mutation_params->mutation_rate)
            == CONFIG_FALSE) {
        fatal_error("mutation_rate is a required parameter");
    }
    if (config_lookup_int(config, "avl_node_block_size", &int_tmp)
            == CONFIG_FALSE) {
        fatal_error("avl_node_block_size is a required parameter");
    }
    msp_set_avl_node_block_size(msp, (size_t) int_tmp);
    if (config_lookup_int(config, "segment_block_size", &int_tmp)
            == CONFIG_FALSE) {
        fatal_error("segment_block_size is a required parameter");
    }
    msp_set_segment_block_size(msp, (size_t) int_tmp);
    if (config_lookup_int(config, "node_mapping_block_size", &int_tmp)
            == CONFIG_FALSE) {
        fatal_error("node_mapping_block_size is a required parameter");
    }
    msp_set_node_mapping_block_size(msp, (size_t) int_tmp);
    if (config_lookup_int(config, "coalescence_record_block_size", &int_tmp)
            == CONFIG_FALSE) {
        fatal_error("coalescence_record_block_size is a required parameter");
    }
    msp_set_coalescence_record_block_size(msp, (size_t) int_tmp);
    if (config_lookup_int(config, "max_memory", &int_tmp)
            == CONFIG_FALSE) {
        fatal_error("max_memory is a required parameter");
    }
    msp_set_max_memory(msp, (size_t) int_tmp * 1024 * 1024);
    ret = read_population_models(msp, config);
    if (config_lookup_string(config, "output_file", &str_tmp)
            == CONFIG_FALSE) {
        fatal_error("output_file is a required parameter");
    }
    /* Create a copy of output_file to return */
    str = malloc(strlen(str_tmp) + 1);
    if (str == NULL) {
        fatal_error("Out of memory");
    }
    strcpy(str, str_tmp);
    *output_file = str;
    config_destroy(config);
    free(config);
    return ret;
}

static void
print_haplotypes(tree_sequence_t *ts)
{
    int ret = 0;
    hapgen_t *hg = calloc(1, sizeof(hapgen_t));
    uint32_t j;
    char *haplotype;

    printf("haplotypes \n");
    if (hg == NULL) {
        ret = MSP_ERR_NO_MEMORY;
        goto out;
    }
    ret = hapgen_alloc(hg, ts);
    if (ret != 0) {
        goto out;
    }
    for (j = 1; j <= ts->sample_size; j++) {
        ret = hapgen_get_haplotype(hg, j, &haplotype);
        if (ret < 0) {
            goto out;
        }
        printf("%d\t%s\n", j, haplotype);
    }
out:
    if (hg != NULL) {
        hapgen_free(hg);
        free(hg);
    }
    if (ret != 0) {
        printf("error occured:%d:%s\n", ret, msp_strerror(ret));
    }
}


static void
print_newick_trees(tree_sequence_t *ts)
{
    int ret = 0;
    newick_converter_t *nc = calloc(1, sizeof(newick_converter_t));
    uint32_t length;
    char *tree;

    printf("converting newick trees\n");
    if (nc == NULL) {
        ret = MSP_ERR_NO_MEMORY;
        goto out;
    }
    ret = newick_converter_alloc(nc, ts, 4);
    if (ret != 0) {
        goto out;
    }
    while ((ret = newick_converter_next(nc, &length, &tree)) == 1) {
        printf("Tree: %d: %s\n", length, tree);
    }
    if (ret != 0) {
        goto out;
    }
out:
    if (nc != NULL) {
        newick_converter_free(nc);
        free(nc);
    }
    if (ret != 0) {
        printf("error occured:%d:%s\n", ret, msp_strerror(ret));
    }
}

static void
print_tree_sequence(tree_sequence_t *ts)
{
    int ret = 0;
    size_t j;
    size_t num_records = tree_sequence_get_num_coalescence_records(ts);
    uint32_t length, mrca;
    sparse_tree_t tree;
    node_record_t *records_in, *records_out, *record;
    coalescence_record_t cr;
    tree_diff_iterator_t *iter = calloc(1, sizeof(tree_diff_iterator_t));
    sparse_tree_iterator_t *sparse_iter = calloc(1, sizeof(sparse_tree_iterator_t));

    if (iter == NULL || sparse_iter == NULL) {
        ret = MSP_ERR_NO_MEMORY;
        goto out;
    }
    printf("Records:\n");
    for (j = 0; j < num_records; j++) {
        if (tree_sequence_get_record(ts, j, &cr, MSP_ORDER_TIME) != 0) {
            fatal_error("tree sequence out of bounds\n");
        }
        printf("\t%d\t%d\t%d\t%d\t%d\t%f\n", cr.left, cr.right, cr.children[0],
                cr.children[1], cr.node, cr.time);
    }
    ret = tree_diff_iterator_alloc(iter, ts);
    if (ret != 0) {
        goto out;
    }
    printf("Tree diffs:\n");
    tree_diff_iterator_print_state(iter);
    while ((ret = tree_diff_iterator_next(
                    iter, &length, &records_out, &records_in)) == 1) {
        tree_diff_iterator_print_state(iter);
        printf("New tree: %d\n", length);
        printf("Nodes In:\n");
        record = records_in;
        while (record != NULL) {
            printf("\t(%d\t%d)\t%d\n", record->children[0],
                    record->children[1], record->node);
            record = record->next;
        }
        printf("Nodes Out:\n");
        record = records_out;
        while (record != NULL) {
            printf("\t(%d\t%d)\t%d\n", record->children[0],
                    record->children[1], record->node);
            record = record->next;
        }
    }
    if (ret != 0) {
        goto out;
    }
    ret = tree_diff_iterator_free(iter);
    if (ret != 0) {
        goto out;
    }
    /* sparse trees */
    ret = tree_sequence_alloc_sparse_tree(ts, &tree, MSP_COUNT_LEAVES);
    if (ret != 0) {
        goto out;
    }
    ret = sparse_tree_iterator_alloc(sparse_iter, ts, &tree);
    if (ret != 0) {
        goto out;
    }
    printf("Sparse trees:\n");
    while ((ret = sparse_tree_iterator_next(sparse_iter)) == 1) {
        printf("New tree: %d (%d)\n", tree.right - tree.left,
                (int) tree.num_nodes);
        sparse_tree_iterator_print_state(sparse_iter);
        /* print some mrcas */
        printf("MRCAS:\n");
        for (j = 1; j <= tree.num_nodes; j++) {
            ret = sparse_tree_get_mrca(&tree, 1, (uint32_t) j, &mrca);
            if (ret != 0) {
                goto out;
            }
            printf("\t%d %d -> %d\n", 1, (int) j, mrca);
        }
    }
    sparse_tree_iterator_free(sparse_iter);
    sparse_tree_free(&tree);
out:
    if (iter != NULL) {
        free(iter);
    }
    if (sparse_iter != NULL) {
        free(sparse_iter);
    }
    if (ret != 0) {
        fatal_error("ERROR: %d: %s\n", ret, msp_strerror(ret));
    }
}

static void
run_simulate(char *conf_file)
{
    int ret = -1;
    int result;
    mutation_params_t mutation_params;
    msp_t *msp = malloc(sizeof(msp_t));
    char *output_file = NULL;
    tree_sequence_t *tree_seq = calloc(1, sizeof(tree_sequence_t));

    if (msp == NULL || tree_seq == NULL) {
        goto out;
    }
    ret = get_configuration(msp, &mutation_params, &output_file, conf_file);
    if (ret != 0) {
        goto out;
    }
    result = msp_run(msp, DBL_MAX, ULONG_MAX);
    if (result < 0) {
        ret = result;
        goto out;
    }
    ret = msp_print_state(msp);
    if (ret != 0) {
        goto out;
    }
    /* Create the tree_sequence from the state of the simulator. */
    ret = tree_sequence_create(tree_seq, msp);
    if (ret != 0) {
        goto out;
    }
    ret = tree_sequence_generate_mutations(tree_seq,
            mutation_params.mutation_rate, mutation_params.random_seed);
    if (ret != 0) {
        goto out;
    }
    int j;
    for (j = 0; j < 1; j++) {
        ret = tree_sequence_dump(tree_seq, output_file, 0);
        if (ret != 0) {
            goto out;
        }
        tree_sequence_free(tree_seq);
        memset(tree_seq, 0, sizeof(tree_sequence_t));
        ret = tree_sequence_load(tree_seq, output_file, 0);
        if (ret != 0) {
            goto out;
        }
    }
    print_tree_sequence(tree_seq);
    if (0) {
        tree_sequence_print_state(tree_seq);
        print_haplotypes(tree_seq);
        print_newick_trees(tree_seq);
        tree_sequence_print_state(tree_seq);
    }
out:
    if (msp != NULL) {
        msp_free(msp);
        free(msp);
    }
    if (tree_seq != NULL) {
        tree_sequence_free(tree_seq);
        free(tree_seq);
    }
    if (output_file != NULL) {
        free(output_file);
    }
    if (ret != 0) {
        printf("error occured:%d:%s\n", ret, msp_strerror(ret));
    }
}

int
main(int argc, char** argv)
{
    if (argc != 2) {
        fatal_error("usage: %s CONFIG_FILE", argv[0]);
    }
    run_simulate(argv[1]);
    return EXIT_SUCCESS;
}
