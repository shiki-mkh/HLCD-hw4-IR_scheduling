#include "common.h"
#include <minisat/core/Solver.h>
#include <list>
#include <queue>
#include <vector>
#include <iostream>
using namespace std;

void calc_ASAP(DFG *dfg, const vector<Op *> &ops, const vec2d<int> &deps, const vec2d<int> &uses)
{

    // 拓扑排序求asap
}
struct cmp
{
    bool operator()(const pair<int, int> &a, const pair<int, int> &b) const
    {
        return a.first > b.first; // 按time从小到大排序
    }
};
priority_queue<pair<int, int>, vector<pair<int, int>>, cmp> op_finish;

vector<int> membusy;
void schedule(DFG *dfg, const vector<Op *> &ops, double clock_period)
{

    list<pair<double, int>> ready_list;
    auto compare = [](const std::pair<double, int> &a, const std::pair<double, int> &b)
    {
        return a.second < b.second; // 按照第一个元素升序排序
    };
    auto comb = [](const std::pair<double, int> &a, const std::pair<double, int> &b)
    {
        return a.first < b.first;
    };

    vec2d<int> deps, uses;
    get_deps_and_uses(dfg, deps, uses);

    membusy.resize(dfg->num_memory, 0);

    // start
    // calc_ASAP( dfg , ops ,deps ,uses);

    int cnt_sched = 0;
    // 初始化ready list
    for (int i = 0; i < dfg->stmts.size(); i++)
    {
        if (deps[i].empty())
        {
            ready_list.push_back(make_pair(dfg->stmts[i]->op->delay, i));
            // TODO: 将ready——list的第一位改成asap order
            // 不止这里，以后所有用到readylist的地方都要改
            // TODO：优化combination逻辑
        }
    }
    // List Schedule
    int l = 1;
    while (!ready_list.empty() || cnt_sched < dfg->stmts.size())
    {

        // release op
        ready_list.sort(compare);
        while (!op_finish.empty() && op_finish.top().first <= l)
        {
            Op *release = dfg->stmts[op_finish.top().second]->op;
            if (dfg->stmts[op_finish.top().second]->is_mem_stmt())
                membusy[dfg->stmts[op_finish.top().second]->get_arr_idx()]--;
            else
                release->busy--;
            for (int to : uses[op_finish.top().second])
            {
                dfg->stmts[to]->indeg++;
                // add to list
                if (dfg->stmts[to]->start_cycle == -1 && dfg->stmts[to]->indeg == deps[to].size())
                    ready_list.push_front(make_pair(dfg->stmts[to]->op->delay, to));
            }

            op_finish.pop();
        }

        ready_list.sort(compare);

        for (auto it = ready_list.begin(); it != ready_list.end();)
        {
            Op *k = dfg->stmts[it->second]->op;
            // store / load
            if (dfg->stmts[it->second]->is_mem_stmt())
            {
                int mem_idx = dfg->stmts[it->second]->get_arr_idx();
                if (membusy[mem_idx] + 1 <= k->limit || k->limit == -1)
                {
                    dfg->stmts[it->second]->start_cycle = l;
                    cnt_sched++;
                    membusy[mem_idx]++;
                    op_finish.push(make_pair(l + max(k->latency, 1), it->second));

                    it = ready_list.erase(it);
                }

                else
                    it++;
            }
            //
            else
            {
                if (k->busy + 1 <= k->limit || k->limit == -1)
                {
                    // Schedule op @ cycle l
                    dfg->stmts[it->second]->start_cycle = l;
                    cnt_sched++;
                    k->busy++;
                    op_finish.push(make_pair(l + max(k->latency, 1), it->second));

                    it = ready_list.erase(it);
                }

                else
                    it++;
            }
        }
        // update ready_list //修改入度

        // update_ready_list();

        l = l + 1;
    }
}
