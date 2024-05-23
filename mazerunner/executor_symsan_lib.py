import copy
import symsan
import os
import ctypes
import logging
import time

from defs import *
import utils
from agent import ExploitAgent, RecordAgent, ExploreAgent

class ConcolicExecutor:
    
    class Timer:
        def reset(self):
            self.proc_start_time = int(time.time() * utils.MILLION_SECONDS_SCALE)
            self.proc_end_time = self.proc_start_time
            self.solving_time = 0

        def execution_timeout(self, timeout):
            curr_time = int(time.time() * utils.MILLION_SECONDS_SCALE)
            total_time = curr_time - self.proc_start_time
            return total_time >= timeout * utils.MILLION_SECONDS_SCALE

    def __init__(self, config, agent, output_dir):
        self.config = config
        self.cmd = config.cmd
        self.agent = agent
        self.timer = ConcolicExecutor.Timer()
        self.logger = logging.getLogger(self.__class__.__qualname__)
        self.logging_level = config.logging_level
        self.generated_files = []
        # symsan lib instance
        symsan.init(self.cmd[0])
        self.symsan_tasks = []
        # options
        self.testcase_dir = output_dir
        self.record_mode_enabled = True if type(agent) is RecordAgent else False
        self.onetime_solving_enabled = True if (type(agent) is ExploitAgent) else False
        self.save_seed_info = True if (type(agent) is ExploreAgent or type(agent) is ExploitAgent) else False
        self.gep_solver_enabled = config.gep_solver_enabled
        utils.disable_core_dump()

    def tear_down(self):
        symsan.destroy()

    def get_result(self):
        # TODO: implement stream reader thread in case the subprocess closes
            return ExecutorResult(self.timer.proc_end_time - self.timer.proc_start_time, 
                                  self.timer.solving_time, self.agent.min_distance, 
                                  self.proc.returncode, self.msg_num, 
                                  self.generated_files, self.proc.stdout, self.proc.stderr)

    def setup(self, input_file, session_id=0):
        self._session_id = session_id
        self.input_file = input_file
        self.msg_num = 0
        self.symsan_tasks.clear()
        self.generated_files.clear()
        self.agent.reset()
        self.timer.reset()

    def run(self, timeout=None):
        # create and execute the child symsan process
        logging_level = 1 if self.logging_level == logging.DEBUG else 0
        cmd, stdin, self.input_content = utils.fix_at_file(self.cmd, self.input_file)
        self.logger.debug("Executing %s" % ' '.join(cmd))
        
        if stdin:
            symsan.config("stdin", args=cmd, debug=logging_level, bounds=0)
            symsan.run(stdin=self.input_file)
        else:
            symsan.config(self.input_file, args=cmd, debug=logging_level, bounds=0)
            symsan.run()
        symsan.init_parser([self.input_content])

    def process_request(self):
        self.timer.solving_time = 0
        should_handle = True
        self.msg_num = 0
        # we don't need to check self.has_terminated here
        # because the pipe might still be readable even if the child process has terminated
        while should_handle:
            e = symsan.read_event(ctypes.sizeof(pipe_msg))
            if len(e) < ctypes.sizeof(pipe_msg):
                break
            start_time = int(time.time() * utils.MILLION_SECONDS_SCALE)
            msg = pipe_msg.from_buffer_copy(e)
            self.logger.debug(
                "process_request: received msg. "
                "msg_type=%s, flags=%s, instance_id=%s, addr=%s, context=%s, "
                "id=%s, label=%s, result=%s",
                msg.msg_type, msg.flags, msg.instance_id, hex(msg.addr),
                msg.context, msg.id, msg.label, msg.result
            )
            if msg.msg_type == MsgType.cond_type.value:
                solving_status = self._process_cond_request(msg)
                if ((solving_status == SolvingStatus.SOLVED_NESTED or solving_status == SolvingStatus.SOLVED_OPT_NESTED_TIMEOUT) 
                    and self.onetime_solving_enabled):
                    should_handle = False
                if (solving_status == SolvingStatus.UNSOLVED_UNKNOWN
                    or solving_status == SolvingStatus.UNSOLVED_INVALID_EXPR):
                    self.logger.error(f"process_request: slover panic, stop processing. "
                                      f"solving_status={solving_status}")
                    should_handle = False
            elif msg.msg_type == MsgType.gep_type.value:
                self._process_gep_request(msg)
            elif msg.msg_type == MsgType.memcmp_type.value:
                self.solver.handle_memcmp(msg, self.pipefds[0])
            elif msg.msg_type == MsgType.fsize_type.value:
                pass
            elif msg.msg_type == MsgType.fini_type.value:
                self.agent.min_distance = min(msg.result, self.agent.min_distance)
            else:
                self.logger.error(f"process_request: Unknown message type: {msg.msg_type}")
            end_time = int(time.time() * utils.MILLION_SECONDS_SCALE)
            self.timer.solving_time += end_time - start_time
            self.msg_num += 1

    def _process_cond_request(self, msg):
        state_data = symsan.read_event(ctypes.sizeof(mazerunner_msg))
        if len(state_data) < ctypes.sizeof(mazerunner_msg):
            self.logger.error(f"__process_cond_request: mazerunner_msg too small: {len(state_data)}")
            return SolvingStatus.UNSOLVED_INVALID_MSG
        state_msg = mazerunner_msg.from_buffer_copy(state_data)
        self.agent.handle_new_state(state_msg, msg.result, msg.label)
        if not msg.label:
            return SolvingStatus.UNSOLVED_INVALID_MSG
        if self.record_mode_enabled:
            return SolvingStatus.UNSOLVED_UNINTERESTING_COND
        is_interesting = self.agent.is_interesting_branch()
        tasks = symsan.parse_cond(msg.label, msg.result, msg.flags)
        
        if not is_interesting:
            return SolvingStatus.UNSOLVED_UNINTERESTING_COND
        
        solution = []
        status = [] 
        for task in tasks:
            r, sol = symsan.solve_task(task)
            solution += sol
            s = self._parse_solving_status(r)
            status.append(s)
        
        seed_info = ''
        if self.save_seed_info:
            reversed_sa = str(self.agent.curr_state.reversed_sa) if is_interesting else ''
            score = self.agent.compute_branch_score() if is_interesting else ''
            seed_info = f"{score}:{reversed_sa}"
        self._generate_testcases(solution, seed_info)
        solving_status = self._handle_solving_status(status)
        return solving_status

    def _process_gep_request(self, msg):
        #TODO: implemnet gep solver
        pass

    def _generate_testcases(self, solution, seed_info):
        input_buf = copy.copy(bytearray(self.input_content))
        changed_index = set()
        for (_, i, v) in solution:
            assert i < len(input_buf)
            assert i not in changed_index
            input_buf[i] = v
            changed_index.add(i)
        fname = f"id-0-{self._session_id}-{len(self.generated_files)}"
        if seed_info:
            fname += "," + seed_info
        path = os.path.join(self.testcase_dir, fname)
        with open(path, "wb") as f:
            f.write(input_buf)
        self.generated_files.append(fname)
    
    def _handle_solving_status(self, status):
        nested_solved = True
        for s in status:
            if s == SolvingStatus.UNSOLVED_UNINTERESTING_SAT:
                return SolvingStatus.UNSOLVED_UNINTERESTING_SAT
            if s == SolvingStatus.UNSOLVED_PRE_UNSAT:
                return SolvingStatus.UNSOLVED_PRE_UNSAT
            if s == SolvingStatus.UNSOLVED_OPT_UNSAT:
                self.agent.handle_unsat_condition(SolvingStatus.UNSOLVED_OPT_UNSAT)
                return SolvingStatus.UNSOLVED_OPT_UNSAT
            if s == SolvingStatus.UNSOLVED_TIMEOUT:
                self.agent.handle_unsat_condition(SolvingStatus.UNSOLVED_TIMEOUT)
                return SolvingStatus.UNSOLVED_TIMEOUT
            if s == SolvingStatus.UNSOLVED_INVALID_EXPR:
                return SolvingStatus.UNSOLVED_INVALID_EXPR
            if s == SolvingStatus.UNSOLVED_INVALID_MSG:
                return SolvingStatus.UNSOLVED_INVALID_MSG
            if s == SolvingStatus.UNSOLVED_UNINTERESTING_COND:
                return SolvingStatus.UNSOLVED_UNINTERESTING_COND
            if s == SolvingStatus.UNSOLVED_UNKNOWN:
                return SolvingStatus.UNSOLVED_UNKNOWN
            if s == SolvingStatus.SOLVED_OPT_NESTED_UNSAT:
                self.agent.handle_nested_unsat_condition([])
                return SolvingStatus.SOLVED_OPT_NESTED_UNSAT
            if s == SolvingStatus.SOLVED_OPT_NESTED_TIMEOUT:
                self.agent.handle_nested_unsat_condition([])
                return SolvingStatus.SOLVED_OPT_NESTED_TIMEOUT
            if s != SolvingStatus.SOLVED_NESTED:
                nested_solved = False
        
        if nested_solved:
            return SolvingStatus.SOLVED_NESTED
        assert False, "Unkown solving status"
    
    def _parse_solving_status(self, r):
        status_map = {
            1: SolvingStatus.UNSOLVED_INVALID_MSG,
            2: SolvingStatus.UNSOLVED_UNKNOWN,
            3: SolvingStatus.UNSOLVED_OPT_UNSAT,
            4: SolvingStatus.UNSOLVED_TIMEOUT,
            5: SolvingStatus.SOLVED_NESTED,
            6: SolvingStatus.SOLVED_OPT_NESTED_UNSAT,
            7: SolvingStatus.SOLVED_OPT_NESTED_TIMEOUT,
            8: SolvingStatus.UNSOLVED_UNKNOWN,
        }
        return status_map.get(r, SolvingStatus.UNSOLVED_UNKNOWN)
