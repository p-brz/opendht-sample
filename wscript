#!/usr/bin/env python
# -*- coding: utf-8 -*-

from os import path
from waflib import Logs

def try_load(ctx, tool, tooldir):
    try:
        ctx.load(tool, tooldir=tooldir)
    except:
        Logs.warn("failed to load tool: " + tool)

def load_tools(ctx):
    ctx.load('compiler_cxx')
    try_load(ctx, 'run_command', "buildtools")

def options(ctx):
    load_tools(ctx)

    ctx.add_option('--debug', action='store_true', default=True, dest='debug', help='Do debug build')
    ctx.add_option('--release', action='store_false', dest='debug', help='Do release build')

def configure(ctx):
    load_tools(ctx)
    ctx.load('conanbuildinfo_waf', tooldir=".");

    # Allows debug build
    if ctx.options.debug:
        ctx.env['CXXFLAGS'] += ['-g']
        ctx.env['CFLAGS'] += ['-g']

    ctx.env['CXXFLAGS'] += ['-std=c++11']
    ctx.env['LDFLAGS'] += ['-pthread']
        
def build(bld):
    bld.recurse('src')
