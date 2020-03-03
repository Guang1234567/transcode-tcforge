#!/usr/bin/python

import sys
import os
import os.path
import optparse
import subprocess

DOC_ID = "*%*"

class TaggedSource(object):
    def __init__(self, f):
        self._f = f
    def __iter__(self):
        return self
    def next(self):
        for line in self._f:
            line = line.strip()
            if DOC_ID not in line[:4]: # is documentation line?
                continue
            line = line[4:].strip()
            if not line:
                continue
            if line == '*%*/':      # end of annotation
                break
            i = line.find('#')
            if i >= 0:
                line = line[:i] # strip comments, if any
            line = line.strip()     # to be sure we're naked ;)
            if line:
                return line
        raise StopIteration

# common XML output ancestor

class TCModuleDoc(object):
    def __init__(self, name, src, srcname=""):
        self._name    = name
        if srcname:
            self._srcname = srcname
        else:
            self._srcname = "unknown"
        if src:
            self.parse(src)

    def to_xml(self):
        # feel free to suggest something nicer.
        res = "<!-- %s -->" %(self._srcname)
        T = self._tree
        if not T:
            return res
        res += "\n" + \
"            <varlistentry>\n" + \
"                <term>\n" + \
"                    <option>%s</option>\n" %(self._name) + \
"                    <emphasis>[%s]</emphasis>\n" %(",".join(T["MEDIA"])) + \
"                </term>\n" + \
"                <listitem>\n" + \
"                    <para>%s</para>\n" %(T["DESCRIPTION"])
        if "BUILD_DEPENDS" in T:
            res += \
"                    <para>At compile time, this module requires: %s</para>\n" %(T["BUILD_DEPENDS"])
        if "DEPENDS" in T:
            res += \
"                    <para>At run time, this module requires: %s</para>\n" %(T["DEPENDS"])
        opttree = T["OPTION"]
        if opttree:
            res += "<para>This module accepts the following options:</para>\n"
        for (n, t), d in opttree.items():
            res += \
"                    <variablelist>\n" + \
"                        <varlistentry>\n" + \
"                            <term>\n" + \
"                                <literal>%s (%s)</literal>\n" %(n, t) + \
"                            </term>\n" + \
"                            <listitem>\n" + \
"                                <para>%s</para>\n" %(d) + \
"                            </listitem>\n" + \
"                        </varlistentry>\n" + \
"                    </variablelist>\n"
        res += "" + \
"                </listitem>\n" + \
"            </varlistentry>"
        return res
    def to_text(self):
        # the simplest thing that works.
        return "\n".join("%s: %s" %(k, self._tree[k]) for k in sorted(self._tree) )
    def parse(self, data):
        raise NotImplementedError
        
## what about decorator pattern or something like that?

class TCModuleDocFromTaggedSource(TCModuleDoc):
    text_secs = set(("DESCRIPTION", "BUILD-DEPENDS", "DEPENDS", "PROCESSING"))
    list_secs = set(("MEDIA", "INPUT", "OUTPUT"))
    opts_secs = set(("OPTION",))

    def _explode(self, opt):
        return opt.replace('*', " (preferred)")
    def _parse_opts(self, line, data, tree, outsecs):
        key, val = line, []
        for line in data:
            if line in outsecs:
                break
            val.append(line)
        n, t = key.split()
        tree[(n, t.strip('()'))] = " ".join(val)
    def parse(self, src):
        self._tree = {}
        this = TCModuleDocFromTaggedSource
        all_secs = this.text_secs|this.list_secs|this.opts_secs
        cur_sec = "" # unspecified/unknown
        for line in src:
            if line in all_secs:
                cur_sec = line
                continue
            if cur_sec in this.text_secs:
                if cur_sec in self._tree:
                    self._tree[cur_sec] += " " + line
                else:
                    self._tree[cur_sec] = line
            if cur_sec in this.list_secs:
                self._tree[cur_sec] = [ o.replace('*', " (preferred)")
                                        for o in line.split(',') ]
            if cur_sec in this.opts_secs:
                if cur_sec not in self._tree:
                    self._tree[cur_sec] = {}
                self._parse_opts(line, src, self._tree[cur_sec], all_secs)

def _main():
    parser = optparse.OptionParser()
    parser.add_option("-s", "--source", dest="use_source",
                      action="store_true", default=False,
                      help="analyse sources of the module")
    parser.add_option("-b", "--binary", dest="use_binary",
                      action="store_true", default=False,
                      help="analyse module objects using tcmodinfo")
    options, args = parser.parse_args()

    if not options.use_source and not options.use_binary:
        print "either source or binary analysis mode must be selected"
        sys.exit(1)

    if options.use_binary:
        print "not yet supported!"
        sys.exit()
    if options.use_source:
        Src = TaggedSource

    print "<!-- begin module list -->"
    for mod in args:
        try:
            modname   = os.path.basename(mod)
            pre, name = modname.split('_')
            root, ext = os.path.splitext(name)
            doc = TCModuleDocFromTaggedSource(root,
                                              TaggedSource(open(mod)),
                                              modname)
            print doc.to_xml()
        except IOError:
            pass # XXX
    print "<!-- end module list -->"

if __name__ == "__main__":
    _main()

# eof
