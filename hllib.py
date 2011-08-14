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


    def extract(self, extr, **kwargs):
        """ Extract function.
        extr - list or dict of items.
            if list and multidir=False (or none):
                all files in list extracted to same directory (outdir=r'C:\path\to\extract\to')
            if list and multidir=True:
                all files extracted to outdir, keeping directory structure.
            if dict:
                e.g. { 'C:\dir01': [ 'root\file.txt', 'root\file.avi' ] }
                all files are extracted to the directory in the keyname.
        kwargs:
            outdir - if applicable, the directory to extract to.
            multidir - set True to name folder per package name and keep directory structure.
            makedirs - set True if missing folders should be automatically created. Defaults False.
        """
        if type(extr) is list:
            if 'multidir' in kwargs and kwargs['multidir'] is True:
                edir = {}
                temp = defaultdict(list)
                for z in extr:
                    psplit = z.split('\\')
                    wdir = os.path.split(z)[0]
                    temp[wdir].append(z) # Add each fn to the appropriate working dir list.
                search = temp.items()
                outdir = True
        elif type(extr) is dict:
            search = extr.items()
            outdir = False
        self.__open__()
        for t in search:
            if outdir is True:
                extr_dir = os.path.join(kwargs['outdir'], self.packagestr, t[0])
            else:
                extr_dir = t[0]
            if not os.path.exists(extr_dir):
                if 'makedirs' in kwargs and kwargs['makedirs'] is True:
                    os.makedirs(extr_dir)
                elif 'makedirs' in kwargs and kwargs['makedirs'] is False:
                    raise Exception('directory path %s does not exist' % extr_dir)
            if type(t[1]) is list and len(t[1]) > 1:
                for f in t[1]:
                    self.pkg.extract(f, extr_dir)
            else:
                self.pkg.extract(t[1], extr_dir)
        self.__close__()


    def validate(self, validr):
        """ Validate function. Validates files when given list as first param. """
        if type(validr) is list:
            self.__open__()
            for z in validr:
                self.pkg.validate(z)
            self.__close__()
        else:
            raise Exception('object is not list')

