import Options
import platform
from os import unlink, symlink, popen
from os.path import lexists, exists

srcdir = '.'
blddir = 'build'
VERSION = '0.1.4'
OSTYPE = platform.system()

def set_options(opt):
  opt.tool_options('compiler_cxx')
  opt.tool_options('compiler_cc')
  opt.add_option('--debug', dest='debug', action='store_true', default=False)
  opt.add_option('--no-bzip', dest='nobzip', action='store_true', default=False)
  opt.add_option('--no-gzip', dest='nogzip', action='store_true', default=False)

def configure(conf):
  conf.check_tool('compiler_cxx')
  conf.check_tool('compiler_cc')
  conf.check_tool('node_addon')

  conf.env.cxxflags = ['-D_FILE_OFFSET_BITS=64', '-D_LARGEFILE_SOURCE', '-Wall']
  conf.env.includes = ['/usr/include', '/usr/local/include']
  conf.env.defines = []
  conf.env.uselibs = []
  conf.env.libpath = ['/usr/lib', '/usr/local/lib']

  if OSTYPE == 'Darwin':
    conf.env.libpath.insert(0, '/opt/local/lib')
    conf.env.includes.insert(0, '/opt/local/include')
    conf.env.cxxflags.append('-mmacosx-version-min=10.4')
  # add any other os special cases here

  if Options.options.debug:
    conf.env.defines += ['DEBUG']
    conf.env.cxxflags += ['-O0', '-g3']
  else:
    conf.env.cxxflags += ['-O2']

  if Options.options.nobzip != True:
    conf.check(lib='z', libpath=conf.env.libpath, uselib_store='ZLIB')
    conf.env.defines += ['WITH_GZIP']
    conf.env.uselibs += ['ZLIB']
  if Options.options.nobzip != True:
    conf.check(lib='bz2', libpath=conf.env.libpath, uselib_store='BZLIB')
    conf.env.defines += ['WITH_BZIP']
    conf.env.uselibs += ['BZLIB']

def build(bld):
  obj = bld.new_task_gen('cxx', 'shlib', 'node_addon')
  obj.cxxflags = bld.env.cxxflags
  obj.ldflags = []

  if OSTYPE == 'Darwin':
    obj.ldflags.append('-mmacosx-version-min=10.4')
  # finalize any other os special cases here

  obj.target = 'gzbz2'
  obj.source = 'compress.cc'
  obj.defines = bld.env.defines
  obj.uselib = bld.env.uselibs
  obj.libpath = bld.env.libpath

def shutdown(bld):
  if Options.commands['clean'] and not Options.commands['build']:
    if lexists('gzbz2.node'):
      unlink('gzbz2.node')
  elif Options.commands['build']:
    if exists('build/Release/gzbz2.node') and not lexists('gzbz2.node'):
      symlink('build/Release/gzbz2.node', 'gzbz2.node')
