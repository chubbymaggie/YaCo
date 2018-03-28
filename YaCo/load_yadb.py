#   Copyright (C) 2017 The YaCo Authors
#
#   This program is free software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program.  If not, see <http://www.gnu.org/licenses/>.

import argparse
import idaapi
import idc
import logging
import os
import sys

prog = idc.ARGV[0] if len(idc.ARGV) else None
parser = argparse.ArgumentParser(prog=prog, description="Import to IDA database")
parser.add_argument("bin_dir", type=os.path.abspath, help="YaCo bin directory")
parser.add_argument("filename", type=os.path.abspath, help="Input yadb database")
parser.add_argument("--no-exit", action="store_true", help="Do not exit IDA when done")
parser.add_argument("--quick", action="store_true", help="Skip IDA auto-analysis")
args = parser.parse_args(idc.ARGV[1:])

root_dir = os.path.abspath(os.path.join(args.bin_dir, '..'))
for path in ['bin', 'YaCo']:
    sys.path.append(os.path.join(root_dir, path))

# import yatools dependencies
if idc.__EA64__:
    import YaToolsPy64 as ya
else:
    import YaToolsPy32 as ya

idc.Wait()
name, _ = os.path.splitext(idc.GetIdbPath())
ya.import_to_ida(name, args.filename)
if not args.quick:
    idc.Wait()

idaapi.cvar.database_flags = idaapi.DBFL_COMP
if not args.no_exit:
    idc.Exit(0)
