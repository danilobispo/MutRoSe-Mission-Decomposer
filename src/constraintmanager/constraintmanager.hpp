#ifndef __CONSTRAINT_MANAGER
#define __CONSTRAINT_MANAGER

#include <vector>
#include <map>
#include <string>
#include <variant>
#include <stack>
#include <queue>
#include <set>
#include <iostream>

#include "../missiondecomposer/missiondecomposer.hpp"
#include "../gm/gm.hpp"
#include "../utils/constraint.hpp"

class ConstraintManager {
    public:
        ConstraintManager(GMGraph g, ATGraph md, bool verb);

        std::vector<Constraint> generate_mission_constraints();

        void generate_at_constraints(ATGraph trimmed_mission_decomposition);
        void transform_at_constraints();
        void generate_execution_constraints();

        void generate_constraints_from_stacks(std::stack<std::pair<int,ATNode>>& operators_stack, std::stack<std::variant<std::pair<int,ATNode>,Constraint>>& nodes_stack, std::map<int,std::set<int>>& existing_constraints);
        void generate_par_and_fallback_constraints(std::map<int,std::set<int>>& existing_constraints, std::stack<std::variant<std::pair<int,ATNode>,Constraint>>& current_branch_nodes_stack,
                                                    std::stack<std::pair<int,ATNode>>& current_branch_operators_stack, constraint_type ctype);
        void generate_seq_constraints(std::map<int,std::set<int>>& existing_constraints, std::stack<std::variant<std::pair<int,ATNode>,Constraint>>& current_branch_nodes_stack,
                                        std::stack<std::pair<int,ATNode>>& current_branch_operators_stack, std::string last_op);
        
        void trim_mission_constraints();
        void check_execution_constraints();

        Constraint generate_constraint(std::pair<int,ATNode> n1, std::pair<int,ATNode> n2, constraint_type type);
    
    private:
        ATGraph mission_decomposition;
        GMGraph gm;
        bool verbose;
        std::vector<Constraint> mission_constraints;
};



#endif