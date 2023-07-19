import os
import pickle

from utils import mkdir

class RLModel:
    def __init__(self, config):
        if config.mazerunner_dir:
            self.output = config.mazerunner_dir
            mkdir(self.my_dir)
        self.visited_sa = set()
        self.Q_table = {}
        self._unreachable_branches = []

    @property
    def my_dir(self):
        return os.path.join(self.output, "model")

    def save(self):
        with open(os.path.join(self.my_dir, "visited_sa"), 'wb') as fp:
            pickle.dump(self.visited_sa, fp, protocol=pickle.HIGHEST_PROTOCOL)
        with open(os.path.join(self.my_dir, "Q_table"), 'wb') as fp:
            pickle.dump(self.Q_table, fp, protocol=pickle.HIGHEST_PROTOCOL)  
        with open(os.path.join(self.my_dir, "unreachable_branches"), 'wb') as fp:
            pickle.dump(self._unreachable_branches, fp, protocol=pickle.HIGHEST_PROTOCOL)
    
    def load(self):
        visited_sa_path = os.path.join(self.my_dir, "visited_sa")
        if os.path.isfile(visited_sa_path):
            with open(visited_sa_path, 'rb') as fp:
                self.visited_sa = pickle.load(fp)
        Q_table_path = os.path.join(self.my_dir, "Q_table")
        if os.path.isfile(Q_table_path):
            with open(Q_table_path, 'rb') as fp:
                self.Q_table = pickle.load(fp)
        unreachable_branches_path = os.path.join(self.my_dir, "unreachable_branches")
        if os.path.isfile(unreachable_branches_path):
            with open(unreachable_branches_path, 'rb') as fp:
                self._unreachable_branches = pickle.load(fp)
