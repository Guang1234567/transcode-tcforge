#!/usr/bin/python
# 
#Copyright (c) 2009-2010 Francesco Romani <fromani at gmail dot com>
#
#This software is provided 'as-is', without any express or implied
#warranty. In no event will the authors be held liable for any damages
#arising from the use of this software.
#
#Permission is granted to anyone to use this software for any purpose,
#including commercial applications, and to alter it and redistribute it
#freely, subject to the following restrictions:
#
#    1. The origin of this software must not be misrepresented; you must not
#    claim that you wrote the original software. If you use this software
#    in a product, an acknowledgment in the product documentation would be
#    appreciated but is not required.
#
#    2. Altered source versions must be plainly marked as such, and must not be
#    misrepresented as being the original software.
#
#    3. This notice may not be removed or altered from any source
#    distribution.
#

# IMPORTANT NOTE:
# don't forget to add to PYTHONPATH the directory where gtranscode2.py is
# before to run those tests.

import subprocess
import unittest
import os.path

import sys
# hack to make the testing easier.
where = os.path.abspath(os.path.dirname(sys.argv[0]))
src   = os.path.join(where, '..', 'src')
sys.path.append(src)
del where
del src
# hack ends here

import gtranscode2 as tci

class ConfigManagerProfilesTest(unittest.TestCase):
    def setUp(self):
        bins = tci.TCBinaries()
        self.cfg = tci.TCConfigManager(bins)
    def test00_Creation(self):
        assert self.cfg
    def test01_HaveProfilePath(self):
        assert os.path.exists(self.cfg._profile_path)
    def test02_HaveProfiles(self):
        assert(len(self.cfg.profiles) > 1)
    def test03_ExistsProfiles(self):
        def _makepath(path, names):
            return [ os.path.join(path, "%s.cfg" % n) for n in names ]
        path = self.cfg._profile_path # shortcut
        assert all(os.path.exists(p) for p in _makepath(path, self.cfg.profiles))


class TCSourceFakeProbeTest(unittest.TestCase):
    def setUp(self):
        self.src = tci.TCSourceFakeProbe()
    def test00_Creation(self):
        assert self.src
    def test01_NamedCreation(self):
        self.cfg = tci.TCSourceFakeProbe("test")
        assert self.src
    def test02_Path(self):
        assert self.src.path == "N/A"
    def test03_NamedPath(self):
        path = "test"
        self.src = tci.TCSourceFakeProbe(path)
        assert self.src.path == path
    def test04_AttributeNumber(self):
        assert len(self.src.info) == len(tci.TCSourceFakeProbe._remap)
    def test05_AttributeValueEmpty(self):
        for k, v in self.src.info.items():
            assert k
            assert v == ""


if __name__ == "__main__":
    unittest.main()

