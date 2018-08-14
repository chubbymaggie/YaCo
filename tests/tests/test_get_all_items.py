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

#!/bin/python

import inspect
import os
import runtests
import unittest


class Fixture(runtests.Fixture):

    def test_get_all_items_qt54(self):
        a, _ = self.setup_repos()
        self.check_range(a, 0x66023FE9, 0x66024012, """
0x66023fed: data: data align
0x66023ff0: data: data dword comm ref name 0:off
0x66024004: data: data byte comm ref labl
""")
        # first data item has prev line comment
        self.check_range(a, 0x66001000, 0x6600100F, """
0x66001000: data: data byte line
0x66001005: block: code func ref name
0x6600100a: block: code func ref name
""")
        self.check_range(a, 0x6600DA80, 0x6600DAEA, """
0x6600da80: block: code func ref labl
""")
        self.check_range(a, 0x6605E140, 0x6605E198, """
0x6605e140: data: data strlit labl name
0x6605e196: data: data align
""")
        self.check_range(a, 0x6605E1B6, 0x6605E1EB, """
""")
        # two items have comments
        self.check_range(a, 0x66066EE8, 0x66066EF4, """
0x66066ee8: data: data dword comm 0:numd
0x66066eec: data: data dword comm 0:off
""")
        self.check_range(a,  0x66071e04, 0x66071e0c, """
0x66071e09: unexplored: unkn ref labl
""")
        self.check_range(a, 0x6604F4D0, 0x6604F4F5, """
0x6604f4d0: block: code func ref labl
""")
        # create a function with undefined data in the middle
        # by concatenating two functions with junk in between
        a.run(
            self.script("""
ea = 0x6600EB70
idc.add_func(ea)
ida_auto.plan_and_wait(ea, idc.find_func_end(ea))
idc.set_func_end(ea, ea+0x6C)
"""),
        )
        self.check_range(a, 0x6600EB70, 0x6600EBDC, """
0x6600eb70: block: code func ref labl
""")

    def test_get_all_items_cmder(self):
        a, _ = self.setup_cmder()
        self.check_range(a, 0x00403070, 0x004035E4, """
0x403070: block: code func ref name
0x4032d3: data: data align
0x4032d4: data: data dword comm ref name 0:off
0x4032eb: data: data align
0x4032f7: data: data align
0x403309: data: data align
0x403323: data: data align
0x40337f: data: data align
0x403380: data: data dword comm ref name 0:off
0x403397: data: data align
0x4033a5: data: data align
0x4033bb: data: data align
0x4034bd: data: data align
0x4035a7: data: data align
""")

    @unittest.skip("only use manually")
    def test_full_all_items(self):
        full = None
        golden_filename = "test_get_all_items.700.golden"
        expected_path = os.path.join(os.path.dirname(inspect.getsourcefile(lambda:0)), golden_filename)
        a, _ = self.setup_repos()
        if False:
            got = self.check_range(a, 0x66001000, 0x66073F5C, None)
            with open(expected_path, "wb") as fw:
                with open(got, "rb") as fr:
                    fw.write(fr.read())
        with open(expected_path, "rb") as fh:
            full = fh.read()
        self.check_range(a, 0x66001000, 0x66073F5C, full)

    def test_undefined_data_items(self):
        a, b = self.setup_cmder()
        self.check_range(a, 0x0041C2DF, 0x0041C30C, """
0x41c2e0: data: data dword ref labl
0x41c2e4: data: data dword ref labl
0x41c2e8: data: data dword ref labl
0x41c2ec: data: data dword ref labl
0x41c2f0: unexplored: unkn ref labl
0x41c2fc: data: data dword ref labl
0x41c300: data: data byte ref labl
0x41c301: data: data align
0x41c304: data: data dword ref labl
0x41c308: data: data dword ref labl
""")

        # modify undefined data
        a.run(
            self.script("""
ea = 0x41C2E0
idaapi.set_name(ea, "another_name")
"""),
            self.save_last_ea(),
        )
        a.check_git(added=["binary", "segment", "segment_chunk", "data"])
        b.run(
            self.check_last_ea(),
        )

        # modify unexplored data
        a.run(
            self.script("""
ea = 0x41BE10
idaapi.set_name(ea, "unexname")
"""),
            self.save_last_ea(),
        )
        a.check_git(added=["data"], modified=["segment_chunk"])
        b.run(
            self.check_last_ea(),
        )
