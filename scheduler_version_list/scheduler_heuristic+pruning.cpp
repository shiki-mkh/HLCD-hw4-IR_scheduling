#include "common.h"
#include <minisat/core/Solver.h>
#include <list>
#include <queue>
#include <vector>
#include <iostream>
using namespace std;

void calc_ASAP(DFG *dfg, const vector<Op *> &ops, const vec2d<int> &deps, const vec2d<int> &uses)
{
    queue<Stmt *> topo_seq;
    vector<Stmt *> topo_out;
    //init
    for (int i = 0; i < dfg->stmts.size(); i++){
        dfg->stmts[i]->indeg=0;
    }
    //求ASAP拓扑排序
        //选出indeg=0的点
    for (int i = 0; i < dfg->stmts.size(); i++){
        if(deps[i].size()==0){
            topo_seq.push(dfg->stmts[i]);            
        }
    }
    while(!topo_seq.empty()){
        Stmt * father = topo_seq.front();
        topo_out.push_back(father);
        for(auto to : uses[father->idx]){
            dfg->stmts[to]->indeg++;
            if(dfg->stmts[to]->indeg == deps[to].size()){
                topo_seq.push(dfg->stmts[to]);
            }
        }
        topo_seq.pop();
    }

    //alap
    int latest_time = 500000000;
    for(auto stmt: dfg->stmts){
        stmt->alap_cycle = 2147483647;
    }
    for(int i =topo_out.size()-1 ;i>=0 ; i-- ){
        if( uses[topo_out[i]->idx].size() ==0){
            topo_out[i]->alap_cycle=latest_time - max(1,topo_out[i]->op->latency)+1;
        }
        else {
            for(auto to: uses[topo_out[i]->idx]){
                topo_out[i]->alap_cycle = min(topo_out[i]->alap_cycle, dfg->stmts[to]->alap_cycle - max(1, topo_out[i]->op->latency));
            }
        }
    }
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

    list<pair<int, int>> ready_list;
    auto compare = [](const std::pair<int, int> &a, const std::pair<int, int> &b)
    {
        return a.second < b.second; // 按照第2个元素升序排序
    };
    auto comb = [](const std::pair<int, int> &a, const std::pair<int, int> &b)
    {
        return a.first < b.first;
    };

    vec2d<int> deps, uses;
    get_deps_and_uses(dfg, deps, uses);

    membusy.resize(dfg->num_memory, 0);

    // start
    calc_ASAP( dfg , ops ,deps ,uses);

    int cnt_sched = 0;
    // 初始化ready list
    for (int i = 0; i < dfg->stmts.size(); i++)
    {
        //init indeg
        dfg->stmts[i]->indeg=0;
        //
        if (deps[i].empty())
        {
            ready_list.push_back(make_pair(dfg->stmts[i]->alap_cycle, i));
            //  将ready——list的第一位改成asap order
            // 不止这里，以后所有用到readylist的地方都要改
            
        }
    }
    // List Schedule
    int l = 1;
    while (!ready_list.empty() || cnt_sched < dfg->stmts.size())
    {

        // release op
        ready_list.sort(comb);
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
                    ready_list.push_front(make_pair(dfg->stmts[to]->alap_cycle, to));
            }

            op_finish.pop();
        }

        ready_list.sort(comb);

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
            else if (k->latency >= 1)
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
            else
                it++;
        }
        // update ready_list //修改入度

        // Schedule_temporal(l);

        // TODO： Schedule_combination(l);
        // TODO： update_readylist();

        bool flag_renew_list = 1;
        while (flag_renew_list)
        {
            flag_renew_list = 0;
            ready_list.sort(comb);
            // 每个循环进行一层schedule
            for (auto it = ready_list.begin(); it != ready_list.end();)
            {
                Op *k = dfg->stmts[it->second]->op;
                // 不是组合逻辑
                if (k->latency >= 1)
                {
                    it++;
                    continue;
                }
                // 求最大延迟
                double dep_delay = 0.0;
                for (auto from : deps[it->second])
                {
                    double from_delay = (dfg->stmts[from]->op->latency == 0) ? dfg->stmts[from]->delay : dfg->stmts[from]->op->delay;
                    if (dfg->stmts[from]->start_cycle == l && from_delay > dep_delay)
                    {
                        dep_delay = from_delay;
                    }
                }
                // schedule在当前周期
                if (dep_delay + k->delay <= clock_period)
                {
                    dfg->stmts[it->second]->start_cycle = l;
                    dfg->stmts[it->second]->delay = dep_delay + k->delay;
                    cnt_sched++;
                    for (int to : uses[it->second])
                    {
                        dfg->stmts[to]->indeg++;

                        // add to list
                        if (dfg->stmts[to]->start_cycle == -1 && dfg->stmts[to]->indeg == deps[to].size())
                        {
                            ready_list.push_front(make_pair(dfg->stmts[to]->alap_cycle, to));
                            flag_renew_list = 1;
                        }
                    }
                    it = ready_list.erase(it);
                }
                // 不能schedule在当前周期
                else
                    it++;
            }
        }

        // update_ready_list();

        l = l + 1;
    }
}
