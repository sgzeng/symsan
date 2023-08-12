import logging
import os
import collections
import pickle
import subprocess
import utils
import random

from config import Config
from defs import TaintFlag
from model import RLModel
from utils import mkdir, bucket_lookup, MAX_BUCKET_SIZE
from learner import BasicQLearner

class ProgramState:
    def __init__(self, distance):
        self.edge = (0, 0)
        self.state = (0,0,0)
        self.action = 0
        self.d = distance
        self.edge_counter = collections.Counter()
    
    def update(self, pc, callstack, action, distance):
        edge = (self.state[0], pc, callstack)
        self.edge_counter.update([edge])
        self.state = (pc, callstack, bucket_lookup(self.edge_counter[edge]))
        self.action = action
        self.d = distance

    def serialize(self):
        return (self.state, self.action, self.d)
    
    def compute_reversed_sa(self):
        reversed_action = 1 if self.action == 0 else 0
        return self.state + (reversed_action, )

class Agent:
    def __init__(self, config: Config):
        self.config = config
        if config.mazerunner_dir:
            self.my_dir = config.mazerunner_dir
            mkdir(self.my_traces)
        self.logger = logging.getLogger(self.__class__.__qualname__)
        self.max_distance = config.max_distance
        self.episode = []
        self._learner = None
        self._model = None

    @property
    def my_traces(self):
        return os.path.join(self.my_dir, "traces")

    @property
    def model(self):
        if not self._model:
            self._model = RLModel(self.config)
        return self._model
    @model.setter
    def model(self, m):
        self._model = m
    def save_model(self):
        if self.config.mazerunner_dir:
            self._model.save()

    @property
    def learner(self):
        if not self._learner:
            self._learner = BasicQLearner(self.model, self.config.discount_factor, self.config.learning_rate)
        return self._learner

    def append_episode(self):
        if (self.curr_state.state[2] < MAX_BUCKET_SIZE
            and self.curr_state.d is not None):
            self.episode.append(self.curr_state.serialize())

    def reset(self):
        self.curr_state = ProgramState(distance=self.max_distance)
        self.episode.clear()
        self.min_distance = self.max_distance

    # for fgtest
    def handle_new_state(self, msg, action):
        pass
    
    def handle_unsat_condition(self):
        pass

    # for fgtest
    def is_interesting_branch(self):
        return True

    def save_trace(self, fn):
        log_path = os.path.join(self.my_traces, fn)
        with open(log_path, 'wb') as fd:
            pickle.dump(self.episode, fd, protocol=pickle.HIGHEST_PROTOCOL)

    def replay_trace(self, trace):
        last_SA = None
        last_reward = 0
        last_d = self.max_distance
        for (next_s, a, d) in trace:
            next_sa = next_s + (a,)
            self.model.add_visited_sa(next_sa)
            reward = self._compute_reward(d, last_d)
            if last_SA:
                self.learner.learn(last_SA, next_s, last_reward)
                self.logger.debug(f"last_SA: {last_SA}, "
                                f"distance: {d if d else 'NA'}, "
                                f"reward: {reward}, "
                                f"Q: {self.model.Q_lookup(last_SA)}")
            last_d = d
            last_SA = next_sa
            last_reward = reward

    def update_curr_state(self, msg, action):
        has_dist = True if msg.flags & TaintFlag.F_HAS_DISTANCE else False
        if has_dist:
            d = msg.avg_dist
        else:
            d = None
        if d and d < self.min_distance:
            self.min_distance = d
        self.curr_state.update(msg.addr, msg.context, action, d)

    def _make_dirs(self):
        utils.mkdir(self.my_traces)

    def _compute_reward(self, d, last_d):
        reward = 0
        if d and last_d:
            assert (d <= self.max_distance and d >= 0 and
                    last_d <= self.max_distance and last_d >= 0)
            reward = last_d - d
        return reward

class RecordAgent(Agent):

    def handle_new_state(self, msg, action):
        d = msg.avg_dist
        self.curr_state.update(msg.addr, msg.context, action, d)
        self.append_episode()

    def is_interesting_branch(self):
        return False

class ReplayAgent(Agent):

    def replay_log(self, log_path):
        with open(log_path, 'rb') as fd:
            trace = list(pickle.load(fd))
            self.replay_trace(trace)

class ExploreAgent(Agent):

    def handle_new_state(self, msg, action):
        self.update_curr_state(msg, action)
        curr_sa = self.curr_state.state + (self.curr_state.action, )
        self.model.remove_target_sa(curr_sa)
        self.append_episode()

    def is_interesting_branch(self):
        if self.curr_state.d is None:
            return False
        reversed_sa = self.curr_state.compute_reversed_sa()
        if reversed_sa in self.model.unreachable_sa:
            return False
        if reversed_sa in self.model.all_target_sa:
            return False
        interesting = reversed_sa not in self.model.visited_sa
        if interesting:
            self.model.add_target_sa(reversed_sa)
        return interesting

class ExploitAgent(Agent):

    def __init__(self, config: Config):
        super().__init__(config)
        self.all_targets = []
        self.last_targets = []
        self.epsilon = config.explore_rate
        self.target = (None, 0) # sa, trace_length

    def handle_new_state(self, msg, action):
        self.update_curr_state(msg, action)
        self.append_episode()
        curr_sa = self.curr_state.state + (self.curr_state.action, )
        if curr_sa == self.target[0] and len(self.episode) == self.target[1]:
            self.target = (None, 0) # sa, trace_length

    def is_interesting_branch(self):
        if self.curr_state.d is None:
            return False
        if self.target[0]:
            return False
        reversed_sa = self.curr_state.compute_reversed_sa()
        if reversed_sa in self.model.unreachable_sa:
            self.logger.debug(f"not interesting, unreachable sa {reversed_sa}")
            return False
        curr_sa = self.curr_state.state + (self.curr_state.action, )
        if curr_sa not in self.model.visited_sa:
            self.logger.debug(f"not interesting, unvisited state")
            return False
        interesting = self.__epsilon_greedy_policy(reversed_sa)
        if interesting:
            self.all_targets.append(reversed_sa)
            self.target = (reversed_sa, len(self.episode))
            self.logger.debug(f"Abort and restart. Target SA: {reversed_sa}")
        return interesting

    def handle_unsat_condition(self):
        self.model.add_unreachable_sa(self.target[0])
        self.target = (None, 0)

    # Return whether the agent should visit the filpped branch.
    def __epsilon_greedy_policy(self, reversed_sa):
        if (reversed_sa not in self.model.visited_sa
            and random.random() < self.epsilon):
            self.logger.debug(f"interesting, epsilon-greedy policy")
            return True
        if (reversed_sa in self.model.visited_sa
            and random.random() < (self.epsilon ** self.model.visited_sa[reversed_sa])):
            self.logger.debug(f"interesting, epsilon-greedy policy")
            return True
        if self.__greedy_policy() != self.curr_state.action:
            self.logger.debug(f"interesting, greedy policy")
            return True
        else:
            self.logger.debug(f"not interesting, greedy policy")
            return False

    # Returns the greedy action according to the Q value.
    def __greedy_policy(self):
        curr_state_taken = self.model.Q_lookup(self.curr_state.state +(1,))
        curr_state_not_taken = self.model.Q_lookup(self.curr_state.state +(0,))
        if curr_state_taken > curr_state_not_taken:
            return 1
        elif curr_state_taken < curr_state_not_taken:
            return 0
        else:
            return self.curr_state.action
