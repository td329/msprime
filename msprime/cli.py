#
# Copyright (C) 2015 Jerome Kelleher <jerome.kelleher@well.ox.ac.uk>
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

import argparse
import random
import struct
import sys

import msprime


mscompat_description = (
    "mspms is an ms-compatible interface to the msprime library. "
    "It simulates the coalescent with recombination for a variety of "
    "demographic models and outputs the results in a text-based format. "
    "It supports a subset of the functionality available in ms and aims "
    "for full compatibility.")
mscompat_recombination_help = (
    "Recombination at rate rho=4*N0*r where r is the rate of recombination "
    "between the ends of the region being simulated; num_loci is the number "
    "of sites between which recombination can occur")


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
    def __init__(
            self, sample_size=1, num_loci=1, recombination_rate=0,
            num_replicates=1, mutation_rate=0, print_trees=False,
            max_memory="16M", precision=3, population_models=[],
            random_seeds=None):
        self._sample_size = sample_size
        self._num_loci = num_loci
        self._num_replicates = num_replicates
        self._recombination = recombination_rate
        self._mutation_rate = mutation_rate
        self._simulator = msprime.TreeSimulator(sample_size)
        self._simulator.set_max_memory(max_memory)
        self._simulator.set_num_loci(num_loci)
        self._simulator.set_scaled_recombination_rate(recombination_rate)
        self._precision = precision
        self._print_trees = print_trees
        for m in population_models:
            self._simulator.add_population_model(m)
        # sort out the random seeds
        python_seed, ms_seeds = get_seeds(random_seeds)
        self._ms_random_seeds = ms_seeds
        random.seed(python_seed)

    def run(self, output):
        """
        Runs the simulations and writes the output to the specified
        file handle.
        """
        # The first line of ms's output is the command line.
        print(" ".join(sys.argv), file=output)
        print(" ".join(str(s) for s in self._ms_random_seeds), file=output)
        for j in range(self._num_replicates):
            self._simulator.set_random_seed(random.randint(0, 2**30))
            self._simulator.run()
            tree_sequence = self._simulator.get_tree_sequence()
            breakpoints = self._simulator.get_breakpoints()
            print(file=output)
            print("//", file=output)
            if self._print_trees:
                iterator = tree_sequence.newick_trees(
                    self._precision, breakpoints)
                if self._num_loci == 1:
                    for l, ns in iterator:
                        print(ns, file=output)
                else:
                    for l, ns in iterator:
                        # Print these seperately to avoid the cost of creating
                        # another string.
                        print("[{0}]".format(l), end="", file=output)
                        print(ns, file=output)
            if self._mutation_rate > 0:
                # The mutation rate in ms is multiplied by the size of the
                # region
                seed = self._simulator.get_random_seed()
                tree_sequence.generate_mutations(self._mutation_rate, seed)
                hg = msprime.HaplotypeGenerator(tree_sequence)
                s = tree_sequence.get_num_mutations()
                print("segsites:", s, file=output)
                if s != 0:
                    print("positions: ", end="", file=output)
                    positions = [
                        p / self._num_loci for _, p in
                        tree_sequence.get_mutations()]
                    positions.sort()
                    for position in positions:
                        print(
                            "{0:.{1}f}".format(position, self._precision),
                            end=" ", file=output)
                    print(file=output)
                    for h in hg.haplotypes():
                        print(h, file=output)
                else:
                    print(file=output)
            self._simulator.reset()


def positive_int(value):
    int_value = int(value)
    if int_value <= 0:
        msg = "{0} in an invalid postive integer value".format(value)
        raise argparse.ArgumentTypeError(msg)
    return int_value


def create_simulation_runner(args):
    """
    Parses the arguments and returns a SimulationRunner instance.
    """
    num_loci = int(args.recombination[1])
    r = 0.0
    # We don't scale recombination or mutation rates by the size
    # of the region.
    if num_loci > 1:
        r = args.recombination[0] / (num_loci - 1)
    mu = args.mutation_rate / num_loci
    models = []
    # Get the demography parameters
    # TODO for strict ms compatability, we need to resolve the command line
    # ordering of arguments when there are two or more events with the
    # same start time. We should resolve this.
    if args.growth_rate is not None:
        models.append(
            msprime.ExponentialPopulationModel(0.0, args.growth_rate))
    for t, alpha in args.growth_event:
        models.append(msprime.ExponentialPopulationModel(t, alpha))
    for t, x in args.size_event:
        models.append(msprime.ConstantPopulationModel(t, x))
    runner = SimulationRunner(
        sample_size=args.sample_size,
        num_loci=num_loci,
        num_replicates=args.num_replicates,
        recombination_rate=r,
        mutation_rate=mu,
        precision=args.precision,
        max_memory=args.max_memory,
        print_trees=args.trees,
        population_models=models,
        random_seeds=args.random_seeds)
    return runner


def get_parser():
    parser = argparse.ArgumentParser(description=mscompat_description)
    parser.add_argument("sample_size", type=positive_int, help="Sample size")
    parser.add_argument(
        "num_replicates", type=positive_int,
        help="Number of independent replicates")
    parser.add_argument(
        "-V", "--version", action='version',
        version='%(prog)s {}'.format(msprime.__version__))

    group = parser.add_argument_group("Behaviour")
    group.add_argument(
        "--mutation-rate", "-t", type=float, metavar="theta",
        help="Mutation rate theta=4*N0*mu", default=0)
    group.add_argument(
        "--trees", "-T", action="store_true",
        help="Print out trees in Newick format")
    group.add_argument(
        "--recombination", "-r", type=float, nargs=2, default=(0, 1),
        metavar=("rho", "num_loci"), help=mscompat_recombination_help)

    group = parser.add_argument_group("Demography")
    group.add_argument(
        "--growth-rate", "-G", metavar="alpha", type=float,
        help="Population growth rate alpha.")
    group.add_argument(
        "--growth-event", "-eG", nargs=2, action="append",
        type=float, default=[], metavar=("t", "alpha"),
        help="Set the growth rate to alpha at time t")
    group.add_argument(
        "--size-event", "-eN", nargs=2, action="append",
        type=float, default=[], metavar=("t", "x"),
        help="Set the population size to x * N0 at time t")
    group = parser.add_argument_group("Miscellaneous")
    group.add_argument(
        "--random-seeds", "-seeds", nargs=3, type=positive_int,
        metavar=("x1", "x2", "x3"),
        help="Random seeds (must be three integers)")
    group.add_argument(
        "--precision", "-p", type=positive_int, default=3,
        help="Number of values after decimal place to print")
    group.add_argument(
        "--max-memory", "-M", default="100M",
        help=(
            "Maximum memory to use. If the simulation exceeds this limit "
            "exit with error status. Supports K,M and G suffixes"))
    return parser


def mspms_main():
    parser = get_parser()
    args = parser.parse_args()
    if args.mutation_rate == 0 and not args.trees:
        parser.error("Need to specify at least one of --theta or --trees")
    sr = create_simulation_runner(args)
    sr.run(sys.stdout)
