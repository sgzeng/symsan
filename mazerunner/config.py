import json
import pickle
import os
import logging

from model import RLModelType

LOGGING_LEVEL = logging.INFO
MEMORY_LIMIT_PERCENTAGE = 85
DISK_LIMIT_SIZE = 32 * (1 << 30) # 32GB
# Solver configurations
RANDOM_INPUT = "AAAA"
MAX_DISTANCE = float(0x7FFFFFFFFFFFFFFF)
NESTED_BRANCH_ENABLED = True
GEP_SOLVER_ENABLED = False
OPTIMISTIC_SOLVING_ENABLED = True
# Learner configurations
DISCOUNT_FACTOR = 1
LEARNING_RATE = 0.5
EXPLORE_RATE = 0.5
# Executor configurations
SEED_SYNC_FREQUENCY = 100
SAVE_FREQUENCY = 200 # save mazerunner status into disk every SAVE_FREQUENCY executions.
DEFAULT_TIMEOUT = 60
MAX_TIMEOUT = 20 * 60
MAX_ERROR_REPORTS = 30
MAX_CRASH_REPORTS = 30
MAX_FLIP_NUM = 128
# minimum number of hang files to increase timeout
MIN_HANG_FILES = 30
# Model configurations
MODEL_TYPE = "reachability" # "distance"
DECIMAL_PRECISION = 200

class Config:
    __slots__ = ['__dict__',
                 '__weakref__',
                 'agent_type',
                 'logging_level',
                 'random_input',
                 'nested_branch_enabled',
                 'gep_solver_enabled',
                 'optimistic_solving_enabled',
                 'discount_factor',
                 'learning_rate',
                 "output_dir",
                 "afl_dir",
                 "mazerunner_dir",
                 "initial_seed_dir",
                 "mail",
                 "delimiter",
                 "pkglen",
                 "cmd",
                 "sync_frequency",
                 "explore_rate",
                 "timeout",
                 "max_timeout",
                 "max_error_reports",
                 "max_crash_reports",
                 "max_flip_num",
                 "min_hang_files",
                 "memory_limit",
                 "disk_limit",
                 "save_frequency",
                 "model_type",
                 "decimal_precision",
                 'max_distance',
                 'initial_policy',
                 'static_result_folder',
    ]

    def __init__(self):
        self._load_default()

    def load(self, path):
        if not path:
            return
        if not os.path.isfile(path):
            raise ValueError(f"{path} does not exist")
        with open(path, 'r') as file:
            new_config = json.load(file)
        for key, value in new_config.items():
            setattr(self, key, value)

    def save(self, path):
        if not path:
            return
        with open(path, 'w') as file:
            json.save(self.__dict__, file)

    def load_args(self, args):
        if args.agent_type:
            self.agent_type = args.agent_type
        if args.output_dir:
            self.output_dir = args.output_dir
        if args.afl_dir:
            self.afl_dir = args.afl_dir
        if args.mazerunner_dir:
            self.mazerunner_dir = os.path.join(args.output_dir, args.mazerunner_dir)
        if args.input:
            self.initial_seed_dir = args.input
        if args.mail:
            self.mail = args.mail
        if args.deli:
            self.delimiter = args.deli
        if args.pkglen:
            self.pkglen = args.pkglen
        if args.cmd:
            self.cmd = args.cmd
        if args.debug_enabled:
            self.logging_level = logging.DEBUG
        if args.static_result_folder:
            self.static_result_folder = args.static_result_folder
        if self.static_result_folder:
            distance_file = os.path.join(self.static_result_folder, "distance.cfg.txt")
            self.max_distance = self._load_distance_file(distance_file)
            policy_file = os.path.join(self.static_result_folder, "policy.pkl")
            self.initial_policy = self._load_initial_policy(policy_file)

    def validate(self):
        if not self.cmd:
            raise ValueError("no cmd provided")
        if self.agent_type == "qsym" and not self.afl_dir:
            raise ValueError("You must provide -a option")
        if self.agent_type == "replay" and not self.afl_dir:
            raise ValueError("You must provide -a option")
        if not os.path.isdir(self.output_dir):
            raise ValueError('{self.output_dir} no such directory')

    def _load_default(self):
        self.logging_level = LOGGING_LEVEL
        self.random_input = RANDOM_INPUT
        self.max_distance = MAX_DISTANCE
        self.nested_branch_enabled = NESTED_BRANCH_ENABLED
        self.gep_solver_enabled = GEP_SOLVER_ENABLED
        self.optimistic_solving_enabled = OPTIMISTIC_SOLVING_ENABLED
        self.discount_factor = DISCOUNT_FACTOR
        self.learning_rate = LEARNING_RATE
        self.sync_frequency = SEED_SYNC_FREQUENCY
        self.explore_rate = EXPLORE_RATE
        self.timeout = DEFAULT_TIMEOUT
        self.max_timeout = MAX_TIMEOUT
        self.max_error_reports = MAX_ERROR_REPORTS
        self.max_crash_reports = MAX_CRASH_REPORTS
        self.max_flip_num = MAX_FLIP_NUM
        self.min_hang_files = MIN_HANG_FILES
        self.memory_limit = MEMORY_LIMIT_PERCENTAGE
        self.disk_limit = DISK_LIMIT_SIZE
        self.save_frequency = SAVE_FREQUENCY
        self.model_type = RLModelType.reachability
        if MODEL_TYPE == "distance":
            self.model_type = RLModelType.distance
        elif MODEL_TYPE == "reachability":
            self.model_type = RLModelType.reachability
        else:
            self.model_type = RLModelType.unknown
        self.decimal_precision = DECIMAL_PRECISION
        # The other configurations need to be set explicitly by config file or cmd arguments

    def _load_distance_file(self, fp):
        max_distance = -float('inf')
        with open(fp, 'r') as file:
            lines = file.readlines()
            max_distance = max(float(lines[-1].strip().split(',')[-1]), max_distance)
        return max_distance

    def _load_initial_policy(self, fp):
        with open(fp, 'rb') as file:
            policy = pickle.load(file)
        return policy
