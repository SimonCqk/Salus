#! /bin/env python
#
# Copyright 2019 Peifeng Yu <peifeng@umich.edu>
# 
# This file is part of Salus
# (see https://github.com/SymbioticLab/Salus).
# 
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# 
#    http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
from __future__ import print_function, absolute_import, division

import os
import re
import argparse
import pandas as pd
import numpy as np


try:
    from pathlib import Path
except ImportError:
    from pathlib2 import Path  # python 2 backport


ptn_iter = re.compile(r"""(?P<timestamp>.+): \s [sS]tep \s (\d+),\s
                          (loss|perplexity) .*;\s
                          (?P<duration>[\d.]+) \s sec/batch\)?""", re.VERBOSE)
ptn_first = re.compile(r'First iteration: (?P<duration>[\d.]+) sec/batch')
ptn_avg = re.compile(r'Average excluding first iteration: (?P<duration>[\d.]+) sec/batch')
ptn_jct = re.compile(r'JCT: (?P<duration>[\d.]+)\s*s')


def read_iterfile(filepath):
    content = {
        'iters': []
    }
    with open(filepath) as f:
        for line in f:
            line = line.rstrip('\n')
            m = ptn_iter.match(line)
            if m:
                content['iters'].append(float(m.group('duration')))

            m = ptn_first.match(line)
            if m:
                content['first'] = float(m.group('duration'))

            m = ptn_avg.match(line)
            if m:
                content['avg'] = float(m.group('duration'))

            m = ptn_jct.match(line)
            if m:
                content['jct'] = float(m.group('duration'))

    return content


def handle_20iter(target, name, path):
    try:
        content = read_iterfile(path)
        data = {
            'Network': name,
            '20iter-jct': content['jct'],
            '20iter-first': content['first'],
            '20iter-avg': content['avg'],
        }
        target.append(data)
    except (OSError, IOError) as err:
        print('WARNING: file not found: ', err)
    except KeyError as err:
        print('File content is missing {} entries: {}'.format(err.args[0], path))


def handle_1min(target, name, path):
    try:
        content = read_iterfile(path)
        data = {
            'Network': name,
            '1min-jct': content['jct'],
            '1min-num': len(content['iters']),
            '1min-avg': content['avg'],
        }
        target.append(data)
    except (OSError, IOError) as err:
        print('WARNING: file not found: ', err)
    except KeyError as err:
        print('File content is missing {} entries: {}'.format(err.args[0], path))


def handle_5min(target, name, path):
    try:
        content = read_iterfile(path)
        data = {
            'Network': name,
            '5min-jct': content['jct'],
            '5min-num': len(content['iters']),
            '5min-avg': content['avg'],
        }
        target.append(data)
    except (OSError, IOError) as err:
        print('WARNING: file not found: ', err)
    except KeyError as err:
        print('File content is missing {} entries: {}'.format(err.args[0], path))


def handle_10min(target, name, path):
    try:
        content = read_iterfile(path)
        data = {
            'Network': name,
            '10min-jct': content['jct'],
            '10min-num': len(content['iters']),
            '10min-avg': content['avg'],
        }
        target.append(data)
    except (OSError, IOError) as err:
        print('WARNING: file not found: ', err)
    except KeyError as err:
        print('File content is missing {} entries: {}'.format(err.args[0], path))


def generate_csv(logs_dir, output_dir):
    flavors = [
        ('baseline', 'gpu.output'),
        ('salus', 'rpc.output'),
        ('tfdist', 'tfdist.output'),
        ('tfdist-mps', 'tfdist-mps.output'),
        ('mps', 'gpu-mps.output')
    ]
    for casename, file in flavors:
        data = []
        for name in os.listdir(logs_dir):
            path = os.path.join(logs_dir, name)
            if not os.path.isdir(path):
                continue

            if len(name.split('_')) == 2:
                # 20iter
                handle_20iter(data, name, os.path.join(path, file))
            elif len(name.split('_')) == 3:
                # more
                name, mode = name.rsplit('_', 1)
                handlers = {
                    '1min': handle_1min,
                    '5min': handle_5min,
                    '10min': handle_10min,
                }
                if mode not in handlers:
                    print('Unrecognized directory: ', name)
                    continue
                handlers[mode](data, name, os.path.join(path, file))
            else:
                print('Unrecognized directory: ', name)

        def reducenan(x):
            return x[x.notnull()] if x.nunique() == 1 else None

        df = pd.DataFrame(data).groupby('Network').agg(reducenan).reset_index()

        colnames = ['Network', '20iter-jct', '20iter-first', '20iter-avg',
                    '1min-jct', '1min-num', '1min-avg',
                    '5min-jct', '5min-num', '5min-avg',
                    '10min-jct', '10min-num', '10min-avg']
        # add missing
        for col in colnames:
            if col not in df:
                df[col] = np.nan
        # reorder
        df = df[colnames]

        Path(output_dir).mkdir(exist_ok=True)
        df.to_csv(os.path.join(output_dir, 'jct-{}.csv'.format(casename)), index=False)


# Expected names:
# alexnet_25
# alexnet_50
# alexnet_100
# alexnet_25_1min
# alexnet_25_5min
# alexnet_25_10min
if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--logdir', help='Directory contains jct logs', default='logs/osdi18/jct')
    parser.add_argument('--outputdir', help='Directory for output', default='.')
    config = parser.parse_args()

    generate_csv(config.logdir, config.outputdir)
