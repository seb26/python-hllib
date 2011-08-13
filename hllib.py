# Copyright (c) 2011 seb26. All rights reserved.
# Source code is licensed under the terms of the Modified BSD License.

import os
import ctypes
import chllib

class HLLib:


    def __init__(self, package, volatile=False):
        self.package = package
        self.volatile = volatile


    def __open__(self):
        self.pkg = chllib.Package(self.package, volatileaccess=self.volatile)
        return True


    def __close__(self):
        if self.pkg:
            self.pkg.close()
        else:
            return None


    def extract(self, extr, outdir):
        """
        self.__open__()
        for z in extr:
            self.pkg.extract(z, outdir)
        self.__close__()
        """
        return outdir


# HL = HLLib(r'C:\Program Files\Steam\steamapps\team fortress 2 content.gcf')

print chllib.Package(r'C:\Program Files\Steam\steamapps\team fortress 2 content.gcf', volatileaccess=True).extract(r'root\tf\steam.inf', r'X:\test01')

# print HL.extract([ r'root\tf\steam.inf', r'root\tf\scripts\items\items_game.txt' ], r'X:\test01')

