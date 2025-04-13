#include "policy.h"
#include <set>
#include <unordered_map>

constexpr int PREEMPT_THRESHOLD_COUNT = 2;
constexpr int COOLDOWN_TIME = 50;

static std::unordered_map<int, Event::Task> task_info;
static std::unordered_map<int, int> task_last_ready;
static std::unordered_map<int, int> task_last_run;

struct CpuKey {
  int priority;
  int deadline;
  int arrivalTime;
  int taskId;
  bool operator<(CpuKey const& o) const {
    if (priority != o.priority) return priority < o.priority;
    if (deadline != o.deadline) return deadline < o.deadline;
    if (arrivalTime != o.arrivalTime) return arrivalTime < o.arrivalTime;
    return taskId < o.taskId;
  }
};
static std::set<CpuKey> cpu_ready;

struct IoKey {
  int deadline;
  int arrivalTime;
  int taskId;
  bool operator<(IoKey const& o) const {
    if (deadline != o.deadline) return deadline < o.deadline;
    if (arrivalTime != o.arrivalTime) return arrivalTime < o.arrivalTime;
    return taskId < o.taskId;
  }
};
static std::set<IoKey> io_ready;

static int preempt_count = 0;

void insert_cpu(int tid) {
  auto &t = task_info[tid];
  cpu_ready.insert({ t.priority==Event::Task::Priority::kHigh?0:1,
                     t.deadline, t.arrivalTime, tid });
}
void erase_cpu(int tid) {
  for (auto it = cpu_ready.begin(); it != cpu_ready.end(); ++it)
    if (it->taskId == tid) { cpu_ready.erase(it); return; }
}
void insert_io(int tid) {
  auto &t = task_info[tid];
  io_ready.insert({ t.deadline, t.arrivalTime, tid });
}
void erase_io(int tid) {
  for (auto it = io_ready.begin(); it != io_ready.end(); ++it)
    if (it->taskId == tid) { io_ready.erase(it); return; }
}

Action policy(const std::vector<Event>& evs, int cur_cpu, int cur_io) {
  int now = evs.back().time;
  bool needResched = false;

  // 1. 处理事件
  for (auto &e : evs) {
    int id = e.task.taskId;
    switch (e.type) {
      case Event::Type::kTaskArrival:
        task_info[id] = e.task;
        task_last_ready[id] = now;
        insert_cpu(id);
        needResched = true;
        break;
      case Event::Type::kIoRequest:
        erase_cpu(id);
        task_last_ready[id] = now;
        insert_io(id);
        needResched = true;
        break;
      case Event::Type::kIoEnd:
        erase_io(id);
        task_last_ready[id] = now;
        insert_cpu(id);
        needResched = true;
        break;
      case Event::Type::kTaskFinish:
        erase_cpu(id);
        erase_io(id);
        task_info.erase(id);
        task_last_ready.erase(id);
        task_last_run.erase(id);
        needResched = true;
        break;
      case Event::Type::kTimer:
        needResched = true;
        break;
    }
  }

  int next_cpu = cur_cpu;
  int next_io  = cur_io;

  if (needResched) {
    // 2. 低优保底
    if (++preempt_count >= PREEMPT_THRESHOLD_COUNT) {
      for (auto &key : cpu_ready) {
        if (key.priority == 1) {
          next_cpu = key.taskId;
          preempt_count = 0;
          break;
        }
      }
    }

    // 3. 抢占式 EDF + 优先级
    if (next_cpu == cur_cpu) {
      if (!cpu_ready.empty()) {
        auto best = *cpu_ready.begin();
        if (cur_cpu == 0) {
          next_cpu = best.taskId;
        } else if (now - task_last_run[cur_cpu] >= COOLDOWN_TIME) {
          auto &curT = task_info[cur_cpu];
          int curPr = curT.priority==Event::Task::Priority::kHigh?0:1;
          if (best.priority < curPr ||
              (best.priority == curPr && best.deadline < curT.deadline)) {
            next_cpu = best.taskId;
          }
        }
      } else {
        next_cpu = 0;
      }
    }
  }

  // 4. IO 调度：EDF
  if (cur_io == 0 && !io_ready.empty()) {
    next_io = (*io_ready.begin()).taskId;
  }

  if (next_cpu && next_cpu != cur_cpu) {
    task_last_run[next_cpu] = now;
    task_last_ready[next_cpu] = now;
  }
  if (next_io && next_io != cur_io) {
    task_last_run[next_io] = now;
    task_last_ready[next_io] = now;
  }

  if (next_cpu && next_cpu == next_io) next_io = 0;

  return { next_cpu, next_io };
}
