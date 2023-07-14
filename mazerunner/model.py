import os
import pickle

from utils import mkdir

class RLModel:
    def __init__(self, output_dir=None):
        if output_dir:
            self.output = output_dir
            mkdir(self.my_dir)
        self.visited_sa = set()
        self.Q_table = {}
        self._unreachable_branches = []

    @property
    def my_dir(self):
        return os.path.join(self.output, "model")

    def save_model(self, path):
        with open(os.path.join(self.my_dir, "visited_sa"), 'wb') as fp:
            pickle.dump(self.visited_sa, fp, protocol=pickle.HIGHEST_PROTOCOL)
        with open(os.path.join(self.my_dir, "Q_table"), 'wb') as fp:
            pickle.dump(self.Q_table, fp, protocol=pickle.HIGHEST_PROTOCOL)  
        with open(os.path.join(self.my_dir, "unreachable_branches"), 'wb') as fp:
            pickle.dump(self._unreachable_branches, fp, protocol=pickle.HIGHEST_PROTOCOL)
    
    def load_model(self, path):
        with open(os.path.join(self.my_dir, "visited_sa"), 'rb') as fp:
            self.visited_sa = pickle.load(fp)
        with open(os.path.join(self.my_dir, "Q_table"), 'rb') as fp:
            self.Q_table = pickle.load(fp)
        with open(os.path.join(self.my_dir, "unreachable_branches"), 'rb') as fp:
            self._unreachable_branches = pickle.load(fp)

    def load_unreachable_branches(self):
        path = os.path.join(self.my_dir, "unreachable_branches")
        if os.path.isfile(path):
            with open(path, 'rb') as fp:
                self._unreachable_branches = pickle.load(fp)
