import ast
import copy

class Plan:
    def __init__(self, p, r=1):
        assert (all([isinstance(sub, Plan) for sub in p]) 
                or type(p) == tuple)
        self.subplan = p
        self.repeats = r

    def __iter__(self):
        return self.next()

    def merge(self, p):
        return Plan([self, p.copy()])
    
    def copy(self):
        return copy.deepcopy(self)

    def unfold(self):
        if type(self.subplan) == tuple:
            return [self.subplan] * self.repeats
        
        plan = []
        for p in self.subplan:
            plan.extend(p.unfold())
        plan = plan * self.repeats
        return plan

    def next(self):
        if isinstance(self.subplan, tuple):
            for _ in range(self.repeats):
                yield self.subplan
        else:
            for _ in range(self.repeats):
                for subplan in self.subplan:
                    yield from subplan.next()

    def serilize(self):
        if isinstance(self.subplan, tuple):
            return f"({self.subplan},{self.repeats})"
        else:
            subplans_serialized = ','.join([sub.serilize() for sub in self.subplan])
            return f"([{subplans_serialized}],{self.repeats})"

    @staticmethod
    def deserilize(s):
        try:
            data = ast.literal_eval(s)
        except (SyntaxError, ValueError) as e:
            raise ValueError("Invalid serialization format") from e

        def build(obj):
            if isinstance(obj, tuple) and len(obj) == 2:
                sub, repeats = obj
                if isinstance(sub, tuple):
                    return Plan(sub, repeats)
                elif isinstance(sub, list):
                    subplans = [build(item) for item in sub]
                    return Plan(subplans, repeats)
                else:
                    raise ValueError("Invalid subplan type")
            else:
                raise ValueError("Invalid serialization format")

        return build(data)
