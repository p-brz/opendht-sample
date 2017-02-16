#!/usr/bin/env python
# -*- coding: utf-8 -*-

import sys
import subprocess
from os import path
import pipes

from waflib import TaskGen
from waflib.TaskGen import feature
from waflib.Build import BuildContext
from waflib import Logs

def options(ctx):
    RunCommand.instance.options(ctx)

def configure(ctx):
    RunCommand.instance.configure(ctx)

#TODO: support more program types
@feature('cxxprogram', 'cprogram')
@TaskGen.after_method('apply_link')
def register_programs(tgen):
    """Register programs found in tasks from build"""
    RunCommand.instance.add_program_taskgen(tgen)


# Create a new command (run)
class RunContext(BuildContext):
    cmd = 'run'

    def execute(self):
        super(RunContext, self).execute()

        RunCommand.instance.run(self)

class MetaSingleton(type):
    '''
    Metaclass to add static 'instance' property (singleton pattern)
    into RunCommand class (or any other).
    '''

    @property
    def instance(cls):
        try:
            return cls._instance
        except AttributeError:
            cls._instance = cls()
            return cls._instance

    @instance.setter
    def instance(cls, value):
        '''Enable overriding instance class'''
        cls._instance = value

class RunCommand(object):
    __metaclass__=MetaSingleton

    def __init__(self):
        self.programs = {}
        self.run_program = None
        self.run_args = []
        self.registered = False

    def options(self, ctx):
        self.filter_arguments(ctx)

    def configure(self, ctx):
        pass

    def run(self, ctx):
        real_program = self.find_program(self.run_program)
        real_program = path.relpath(real_program) if path.isabs(real_program) else real_program

        args = [x if not ' ' in x else pipes.quote(x) for x in self.run_args]
        p_args = [real_program] + args;

        cmd = " ".join(p_args);

        Logs.pprint("BLUE", "Run: %s" % cmd)

        subprocess.call(cmd, shell=True)

    def find_program(self, program_name):
        if program_name in self.programs:
            return self.programs[program_name]

        return program_name

    def filter_arguments(self, ctx):
        args = sys.argv

        if "run" not in args:
            return

        run_idx = args.index("run")

        # Name after 'run' is the program name
        #TODO: handle error (user does not specify program name)
        self.run_program = args[run_idx + 1]

        # Remaining args will be passed to the program
        self.run_args = args[run_idx + 2 : ];

        # Remove program arguments from sys.argv to avoid waf errors
        del args[run_idx + 1 : ];

    def add_program_taskgen(self, tgen):
        if(getattr(tgen, "link_task", None) and len(tgen.link_task.outputs) > 0):
            # Workaround to store program info across builds
            self.programs[tgen.target] = tgen.link_task.outputs[0].abspath()
