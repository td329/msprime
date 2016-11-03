#
# Copyright (C) 2014 Jerome Kelleher <jerome.kelleher@well.ox.ac.uk>
#
# This file is part of msprime.
#
# msprime is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# msprime is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with msprime.  If not, see <http://www.gnu.org/licenses/>.
#
"""
Command line interfaces to the msprime library.
"""
from __future__ import division
from __future__ import print_function

import os
import sys
import struct
import random
import tempfile
import argparse

import msprime

mscompat_description = """\
An ms-compatible interface to the msprime library. Supports a
subset of the functionality available in ms."""
mscompat_recombination_help="""\
Recombination at rate rho=4*N0*r where r is the rate of recombination
between the ends of the region being simulated; num_loci is the number
of sites between which recombination can occur"""

def get_seeds(random_seeds):
    """
    Takes the specified command line seeds and truncates them to 16
    bits if necessary. Then convert them to a single value that can
    be used to seed the python random number generator, and return
    both values.
    """
    max_seed = 2**16 - 1
    if random_seeds is None:
        seeds = [random.randint(1, max_seed) for j in range(3)]
    else:
        # Follow ms behaviour and truncate back to shorts
        seeds = [s if s < max_seed else max_seed for s in random_seeds]
    # Combine the together to get a 64 bit number
    seed = struct.unpack(">Q", struct.pack(">HHHH", 0, *seeds))[0]
    return seed, seeds


class SimulationRunner(object):
    """
    Class to run msprime simulation and output the results.
    """
    def __init__(self, args):
        self.tree_file_name = None
        self.sample_size = args.sample_size
        self.num_loci = int(args.recombination[1])
        self.rho = args.recombination[0]
        self.num_replicates = args.num_replicates
        self.mutation_rate = args.mutation_rate
        self.print_trees = args.trees
        self.precision = args.precision
        fd, tf = tempfile.mkstemp(prefix="mscompat_", suffix=".dat")
        os.close(fd)
        self.tree_file_name = tf
        self.simulator = msprime.TreeSimulator(self.sample_size,
                self.tree_file_name)
        self.simulator.set_max_memory(args.max_memory)
        self.simulator.set_num_loci(self.num_loci)
        # We don't scale recombination rate by the size of the region.
        if self.num_loci > 1:
            r = self.rho / (self.num_loci - 1)
            self.simulator.set_scaled_recombination_rate(r)
        # Get the demography parameters
        # TODO for strict ms compatability, we need to resolve the command line
        # ordering of arguments when there are two or more events with the
        # same start time. We should resolve this.
        if args.growth_rate is not None:
            m = msprime.ExponentialPopulationModel(0.0, args.growth_rate)
            self.simulator.add_population_model(m)
        for t, alpha in args.growth_event:
            m = msprime.ExponentialPopulationModel(t, alpha)
            self.simulator.add_population_model(m)
        for t, x in args.size_event:
            m = msprime.ConstantPopulationModel(t, x)
            self.simulator.add_population_model(m)
        # sort out the random seeds
        python_seed, ms_seeds = get_seeds(args.random_seeds)
        self.ms_random_seeds = ms_seeds
        random.seed(python_seed)

    def run(self):
        """
        Runs the simulations and writes the output to stdout
        """
        # The first line of ms's output is the command line.
        print(" ".join(sys.argv))
        print(" ".join(str(s) for s in self.ms_random_seeds))
        for j in range(self.num_replicates):
            self.simulator.set_random_seed(random.randint(0, 2**30))
            self.simulator.run()
            msprime.sort_tree_file(self.tree_file_name)
            tf = msprime.TreeFile(self.tree_file_name)
            print()
            print("//")
            if self.print_trees:
                iterator = tf.newick_trees(self.precision)
                if self.num_loci == 1:
                    for l, ns in iterator:
                        print(ns)
                else:
                    for l, ns in iterator:
                        # Print these seperately to avoid the cost of creating another
                        # string.
                        print("[{0}]".format(l), end="")
                        print(ns)
            if self.mutation_rate is not None:
                hg = msprime.HaplotypeGenerator(self.tree_file_name,
                        self.mutation_rate)
                s = hg.get_num_segregating_sites()
                print("segsites:", s)
                if s != 0:
                    print("positions: NOT IMPLEMENTED")
                    for h in hg.get_haplotypes():
                        print(h)
                else:
                    print()
            self.simulator.reset()

    def cleanup(self):
        """
        Cleans up any files created during simulation.
        """
        if self.tree_file_name is not None:
            os.unlink(self.tree_file_name)

def positive_int(value):
    int_value = int(value)
    if int_value <= 0:
        msg = "{0} in an invalid postive integer value".format(value)
        raise argparse.ArgumentTypeError(msg)
    return int_value

def msp_ms_main():
    parser = argparse.ArgumentParser(description=mscompat_description)
    parser.add_argument("sample_size", type=positive_int, help="Sample size")
    parser.add_argument("num_replicates", type=positive_int,
            help="Number of independent replicates")

    group = parser.add_argument_group("Behaviour")
    group.add_argument("--mutation-rate", "-t", type=float, metavar="theta",
            help="Mutation rate theta=4*N0*mu")
    group.add_argument("--trees", "-T", action="store_true",
            help="Print out trees in Newick format")
    group.add_argument("--recombination", "-r", type=float, nargs=2,
            default=(0, 1), metavar=("rho", "num_loci"),
            help=mscompat_recombination_help)

    group = parser.add_argument_group("Demography")
    group.add_argument("--growth-rate", "-G", metavar="alpha", type=float,
            help="Population growth rate alpha.")
    group.add_argument("--growth-event", "-eG", nargs=2, action="append",
            type=float, default=[], metavar=("t", "alpha"),
            help="Set the growth rate to alpha at time t")
    group.add_argument("--size-event", "-eN", nargs=2, action="append",
            type=float, default=[], metavar=("t", "x"),
            help="Set the population size to x * N0 at time t")
    group = parser.add_argument_group("Miscellaneous")
    group.add_argument("--random-seeds", "-seeds", nargs=3, type=positive_int,
            metavar=("x1", "x2", "x3"),
            help="Random seeds (must be three integers)")
    group.add_argument("--precision", "-p", type=positive_int, default=3,
            help="Number of values after decimal place to print")
    group.add_argument("--max-memory", "-M", default="100M",
            help="Maximum memory used. Supports K,M and G suffixes")
    args = parser.parse_args()
    if args.mutation_rate is None and not args.trees:
        parser.error("Need to specify at least one of --theta or --trees")
    sr = SimulationRunner(args)
    try:
        sr.run()
    finally:
        sr.cleanup()
