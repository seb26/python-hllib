# Copyright (c) 2011 seb26. All rights reserved.
# Source code is licensed under the terms of the Modified BSD License.

import os
import ctypes
import chllib
from collections import defaultdict

class HLLib:


    def __init__(self, package, volatile=False):
        self.volatile = volatile
        self.package = package
        if os.path.splitext(package)[1] == '.gcf':
            self.packagestr = package.split('\\')[-1] # the filename of the gcf
        else:
            self.packagestr = package.split('\\')[-2] # the parent directory of the package


    def __open__(self):
        self.pkg = chllib.Package(self.package, volatileaccess=self.volatile)
        return True


    def __close__(self):
        if self.pkg:
            self.pkg.close()
            return True
        else:
            return None


    def extract(self, extr, outdir, **kwargs):
        if type(extr) is list:
            if 'multidir' in kwargs and kwargs['multidir'] is True:
                edir = {}
                temp = defaultdict(list)
                for z in extr:
                    psplit = z.split('\\')
                    wdir = os.path.split(z)[0]
                    temp[wdir].append(z) # Add each fn to the appropriate working dir list.
                self.__open__()
                for t in temp.items():
                    extr_dir = os.path.join(outdir, self.packagestr, t[0])
                    if not os.path.exists(extr_dir):
                        os.makedirs(extr_dir)
                    if len(t[1]) > 1:
                        for f in t[1]:
                            self.pkg.extract(f, extr_dir)
                    else:
                        self.pkg.extract(t[1], extr_dir)
                self.__close__()