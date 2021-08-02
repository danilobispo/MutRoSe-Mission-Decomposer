#include "validmissiongenerator.hpp"

#include <iostream>

#include "../utils/math_utils.hpp"

using namespace std;

/*
    Function: ValidMissionGenerator
    Objective: Class constructor

    @ Input 1: The Task Graph as an ATGraph object
    @ Input 2: The Goal Model as a GMGraph object
    @ Input 3: The vector of mission constraints
    @ Input 4: The initial state of the world (predicates)
    @ Input 5: The initial state of the world (functions)
    @ Input 6: The vector of semantic mappings defined in the configuration file
    @ Input 7: The Goal Model variable mappings (between them and their values)
    @ Input 8: The verbose flag
*/
ValidMissionGenerator::ValidMissionGenerator(ATGraph md, GMGraph g, vector<Constraint> mc, vector<ground_literal> ws, vector<pair<ground_literal,variant<int,float>>> wsf, vector<SemanticMapping> sm, map<string, variant<pair<string,string>,pair<vector<string>,string>>> gmvmap, bool verb, set<string> r_srts, bool pretty) {
    mission_decomposition = md;
    gm = g;
    mission_constraints = mc;
    world_state = ws;
    world_state_functions = wsf;
    semantic_mapping = sm;
    gm_var_map = gmvmap;
    verbose = verb;
    robot_related_sorts = r_srts;
    pretty_print = pretty;
}

/*
    Function: generate_valid_mission_decompositions
    Objective: Generate the valid mission decompositions based on constraints and on the world knowledge. This
    function iniatilizes variables based on the root node and calls a recursive function that performs the generation

    @ Output: The valid mission decompositions vector. A mission decomposition is a vector of pairs of the 
    form ([task_id],[task_node])
*/
pair<vector<vector<pair<int,ATNode>>>,set<Decomposition>> ValidMissionGenerator::generate_valid_mission_decompositions() {
    queue<pair<int,ATNode>> mission_queue = generate_mission_queue();

    /*
        The idea here is:

        -> Go through the queue and recursively build the decompositions (valid ones)
        -> The initial state of the world will be updated depending if we have a sequential or a parallel operator
        
        -> When we find an operator, we need to establish the relation between previous found nodes
        -> If the operator succeeds another operator, we know that we need to relate a task with task already involved in another constraint
        -> If the operator succeds a task, we know that this operator relates to the last two tasks
    */
    map<int,pair<vector<variant<ground_literal,pair<ground_literal,variant<int,float>>>>,vector<pair<literal,vector<string>>>>> effects_to_apply;

    if(!is_unique_branch(mission_decomposition)) {
        recursive_valid_mission_decomposition("", mission_queue, -1, effects_to_apply);
    } else {
        valid_mission_decompositions_from_unique_branch(mission_queue);
    }

    vector<vector<pair<int,ATNode>>> final_valid_mission_decompositions;
    for(auto mission_decomposition : valid_mission_decompositions) {
        final_valid_mission_decompositions.push_back(mission_decomposition.first);
    }

    if(verbose) {
        std::cout << "Valid Mission Decompositions: " << std::endl;
        for(auto mission_decomposition : final_valid_mission_decompositions) {
            std::cout << "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@" << std::endl;
            unsigned int index = 1;
            for(pair<int,ATNode> node : mission_decomposition) {
                if(index == mission_decomposition.size()) {
                    std::cout << get<Decomposition>(node.second.content).id << std::endl;
                } else {
                    std::cout << get<Decomposition>(node.second.content).id << " -> ";
                }
                index++;
            }
            std::cout << "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@" << std::endl << std::endl;
        }
    } else if(pretty_print) {
        std::cout << "-------------------------------------- POSSIBLE MISSION DECOMPOSITIONS --------------------------------------" << std::endl;
        unsigned int mission_index = 1;
        for(auto mission_decomposition : final_valid_mission_decompositions) {
            unsigned int index = 1;
            for(pair<int,ATNode> node : mission_decomposition) {
                Decomposition d = std::get<Decomposition>(node.second.content);

                set<string> required_capabilities;
                for(task t : d.path.decomposition) {
                    for(string cap : t.required_capabilities) {
                        required_capabilities.insert(cap);
                    }
                }

                variant<vector<string>,string> location = d.at.location.first;

                std::cout << "Task [" + d.id << "] ";
                if(required_capabilities.size() > 0) {
                    if(required_capabilities.size() > 1) {
                        std::cout << "with required capabilities [";
                    } else {
                        std::cout << "with required capability [";
                    }

                    int cap_size = required_capabilities.size();
                    int cap_index = 0;
                    for(string cap : required_capabilities) {
                        if(cap_index < cap_size-1) {
                            std::cout << cap << ", ";
                        } else {
                            std::cout << cap << "] ";
                        }

                        cap_index++;
                    }
                }
                if(holds_alternative<vector<string>>(location)) {
                    vector<string> loc = std::get<vector<string>>(location);
                    std::cout << "at locations [";

                    int loc_size = loc.size();
                    int loc_index = 0;
                    for(string l : loc) {
                        if(loc_index < loc_size-1) {
                            std::cout << l << ", ";
                        } else {
                            std::cout << l << "]";
                        }

                        loc_index++;
                    }
                } else {
                    string loc = std::get<string>(location);
                    std::cout << "at location [" + loc + "]";
                }
                if(index == mission_decomposition.size()) {
                    std::cout << std::endl;
                } else {
                    std::cout << " AND" << std::endl;
                }

                index++;
            }

            if(mission_index < final_valid_mission_decompositions.size()) {
                std::cout << std::endl;
            }

            mission_index++;
        }
        std::cout << "-------------------------------------------------------------------------------------------------------------" << std::endl;
    }

    return make_pair(final_valid_mission_decompositions,expanded_decompositions);
}

/*
    Function: recursive_valid_mission_decomposition
    Objective: Generate the valid mission decompositions based on constraints and on the world knowledge. This is the
    recursive function that in fact generates them.

    @ Input 1: The last operation found in the recursive calls
    @ Input 2: A reference to the mission queue
    @ Input 3: The map of effects to be applied
    @ Input 4: The current node depth
    @ Output: Void. The valid mission decompositions will be generated
*/
map<int,pair<vector<variant<ground_literal,pair<ground_literal,variant<int,float>>>>,vector<pair<literal,vector<string>>>>> ValidMissionGenerator::recursive_valid_mission_decomposition(string last_op, queue<pair<int,ATNode>>& mission_queue, int depth,
                                                                                                                        map<int,pair<vector<variant<ground_literal,pair<ground_literal,variant<int,float>>>>,vector<pair<literal,vector<string>>>>> effects_to_apply) {
    /*
        Here we will get the current node and check whether it is an operator or an Abstract Task
    */
   pair<int,ATNode> current_node = mission_queue.front();
   mission_queue.pop();
   depth++;

   map<int,pair<vector<variant<ground_literal,pair<ground_literal,variant<int,float>>>>,vector<pair<literal,vector<string>>>>> children_effects;

   if(holds_alternative<string>(current_node.second.content)) {
       /*
            If we have an operator, we need to check if it is parallel or sequential

            -> The behavior is different depending on which operator we are dealing with
       */
        string op = std::get<string>(current_node.second.content);

        if(op == parallel_op) {
            check_parallel_op_children(mission_queue, children_effects, depth, current_node);
        } else if(op == sequential_op) {
            check_sequential_op_children(mission_queue, children_effects, depth, current_node);
        } else if(op == fallback_op) {
            check_fallback_op_children(mission_queue, children_effects, depth, current_node);
        }

        /*
            -> Check conditions
                - For now, checking of achieve conditions
        */
        check_conditions(children_effects, current_node);

        return children_effects;
    } else {
        vector<pair<int,ATNode>> task_decompositions;

        ATGraph::out_edge_iterator ei, ei_end;
        for(boost::tie(ei,ei_end) = out_edges(current_node.first,mission_decomposition);ei != ei_end;++ei) {
            int target = boost::target(*ei,mission_decomposition);

            if(mission_decomposition[target].node_type == DECOMPOSITION) {
                ATNode d = mission_decomposition[target];

                task_decompositions.push_back(make_pair(target,d));
            }
        }

        map<int,pair<vector<variant<ground_literal,pair<ground_literal,variant<int,float>>>>,vector<pair<literal,vector<string>>>>> decompositions_effects;

        if(valid_mission_decompositions.size() > 0) {
            /*
                Here is the case where we have at least one valid mission decomposition

                -> We must have at least one decomposition valid for each valid mission decomposition
                    - If we don't have, we must raise an error
            */
            vector<pair<vector<pair<int,ATNode>>,set<int>>> new_valid_mission_decompositions;
            for(auto valid_mission_decomposition : valid_mission_decompositions) {
                bool valid_task_decomposition = false; //Check if one task decomposition is valid for this mission decomposition

                for(pair<int,ATNode> task_decomposition : task_decompositions) {
                    Decomposition d = std::get<Decomposition>(task_decomposition.second.content);
                    // World state will be the initial world state + the effects in effects_to_apply (given which tasks are in the set of tasks of the decomposition)
                    vector<ground_literal> ws = world_state;
                    vector<pair<ground_literal,variant<int,float>>> wsf = world_state_functions;

                    map<int,vector<pair<literal,vector<string>>>> ws_ng = non_ground_world_state;
                    
                    apply_effects_in_valid_decomposition(ws, wsf, ws_ng, valid_mission_decomposition, effects_to_apply);

                    vector<pair<int,ATNode>> m_decomposition = valid_mission_decomposition.first;

                    bool preconditions_hold = check_preconditions_for_decomposition(task_decomposition, valid_mission_decomposition, d, ws, wsf, ws_ng);

                    if(preconditions_hold) {
                        /*
                            If preconditions hold we create a new valid mission decomposition with this task decomposition added to the mission decomposition
                        */
                        m_decomposition.push_back(task_decomposition);

                        valid_mission_decomposition.second.insert(task_decomposition.first);

                        pair<vector<pair<int,ATNode>>,set<int>> aux;
                        aux.first = m_decomposition;
                        aux.second = valid_mission_decomposition.second;
                        new_valid_mission_decompositions.push_back(aux);

                        vector<variant<ground_literal,pair<ground_literal,variant<int,float>>>> decomposition_effects;
                        vector<pair<literal,vector<string>>> decomposition_literal_effects;
                        
                        get_effects_from_decomposition(decomposition_effects, decomposition_literal_effects, d);

                        decompositions_effects[task_decomposition.first] = make_pair(decomposition_effects,decomposition_literal_effects);

                        valid_task_decomposition = true;
                    } else {
                        if(verbose) {
                            std::cout << "Preconditions did not hold for task: " << d.id << std::endl;
                        }
                    }
                }

                if(!valid_task_decomposition) {
                    AbstractTask at = get<AbstractTask>(current_node.second.content);
                    string invalid_task_decomposition_error = "NO VALID DECOMPOSITIONS FOR TASK " + at.id + ": " + at.name;

                    throw std::runtime_error(invalid_task_decomposition_error);
                }
            }

            valid_mission_decompositions = new_valid_mission_decompositions;
        } else {
            /*
                Here is the case where we have no valid mission decompositions yet

                -> We don't check for context dependencies since this is the first task and we do not have context dependencies from right to left
            */
            bool at_least_one_decomposition_valid = false;

            for(pair<int,ATNode> task_decomposition : task_decompositions) {
                Decomposition d = get<Decomposition>(task_decomposition.second.content);

                //Check preconditions using the initial world state
                set<int> empty_set;
                bool preconditions_hold = check_decomposition_preconditions(world_state, world_state_functions, non_ground_world_state, make_pair(task_decomposition.first,d), mission_constraints, empty_set, robot_related_sorts);

                if(preconditions_hold) {
                    /*
                        If preconditions hold we create a new valid mission decomposition
                    */
                    Decomposition aux = d;
                    expand_decomposition(d, world_state_functions, verbose);
                    if(aux.path.decomposition.size() != d.path.decomposition.size()) {
                        expanded_decompositions.insert(d);
                    }

                    AbstractTask at1 = get<AbstractTask>(current_node.second.content);
                    pair<vector<pair<int,ATNode>>,set<int>> new_valid_mission;

                    vector<pair<int,ATNode>> new_decomposition;
                    new_decomposition.push_back(task_decomposition);

                    set<int> d_id;
                    d_id.insert(task_decomposition.first);

                    new_valid_mission = make_pair(new_decomposition,d_id);

                    valid_mission_decompositions.push_back(new_valid_mission);

                    vector<variant<ground_literal,pair<ground_literal,variant<int,float>>>> decomposition_effects;
                    vector<pair<literal,vector<string>>> decomposition_literal_effects;
                    
                    get_effects_from_decomposition(decomposition_effects, decomposition_literal_effects, d);

                    decompositions_effects[task_decomposition.first] = make_pair(decomposition_effects,decomposition_literal_effects);

                    at_least_one_decomposition_valid = true;
                } 
            }

            if(!at_least_one_decomposition_valid) {
                AbstractTask at = get<AbstractTask>(current_node.second.content);
                string invalid_task_decomposition_error = "NO VALID DECOMPOSITIONS FOR TASK " + at.id + ": " + at.name;
                
                throw std::runtime_error(invalid_task_decomposition_error);
            }
        }
        
        // Return effects of task here
        return decompositions_effects;
    }
}

void ValidMissionGenerator::valid_mission_decompositions_from_unique_branch(queue<pair<int,ATNode>> mission_queue) {
    while(!mission_queue.empty()) {
        pair<int,ATNode> current_node =  mission_queue.front();

        if(current_node.second.node_type == ATASK) {
            pair<int,ATNode> task_decomposition;

            ATGraph::out_edge_iterator ei, ei_end;
            for(boost::tie(ei,ei_end) = out_edges(current_node.first,mission_decomposition);ei != ei_end;++ei) {
                int target = boost::target(*ei,mission_decomposition);

                if(mission_decomposition[target].node_type == DECOMPOSITION) {
                    ATNode decomposition = mission_decomposition[target];

                    task_decomposition = make_pair(target,decomposition);
                    break;
                }
            }

            Decomposition d = std::get<Decomposition>(task_decomposition.second.content);
            
            bool preconditions_hold = true;
            for(variant<ground_literal,literal> prec : d.prec) {
                for(ground_literal state : world_state) {
                    if(holds_alternative<ground_literal>(prec)) {
                        ground_literal p = std::get<ground_literal>(prec);

                        if(is_same_predicate(p,state)) {
                            if(p.positive != state.positive) {
                                preconditions_hold = false;

                                break;
                            }
                        }
                    }
                }
            }

            if(!preconditions_hold) {
                AbstractTask at = get<AbstractTask>(current_node.second.content);
                string invalid_task_decomposition_error = "NO VALID DECOMPOSITIONS FOR TASK " + at.id + ": " + at.name;

                throw std::runtime_error(invalid_task_decomposition_error);
            }

            vector<pair<int,ATNode>> valid_decomposition;
            valid_decomposition.push_back(task_decomposition);

            set<int> decomposition_id;
            decomposition_id.insert(task_decomposition.first);

            valid_mission_decompositions.push_back(make_pair(valid_decomposition,decomposition_id));
        }

        mission_queue.pop();
    }
}

/*
    Function: check_sequential_op_children
    Objective: Check children of a sequential operator when generating valid mission decompositions.
    Functioning: The basic functioning is:
        -> Go through the queue while the next node in the queue is a child of this operator
            - This is done checking the out edges of the parallel operator node and verifying if the node in the queue is present
        -> For each child we recursively perform decomposition
            - Effects are kept in a children effects map and are applied after each child execution
    
    @ Input 1: A reference to the mission queue
    @ Input 2: A reference to the children effects map
    @ Input 3: The current node depth
    @ Inpui 4: The current node info
    @ Output: Void. The recursive call to the valid missions generation function is performed for each child.
*/
void ValidMissionGenerator::check_sequential_op_children(queue<pair<int,ATNode>>& mission_queue, map<int,pair<vector<variant<ground_literal,pair<ground_literal,variant<int,float>>>>,vector<pair<literal,vector<string>>>>>& children_effects, int depth, pair<int,ATNode> current_node) {
    bool checking_children = true;

    while(checking_children) {
        if(mission_queue.size() == 0) {
            break;
        }
        pair<int,ATNode> next_node = mission_queue.front();
        bool is_child = false;

        ATGraph::out_edge_iterator ei, ei_end;
        for(boost::tie(ei,ei_end) = out_edges(current_node.first,mission_decomposition);ei != ei_end;++ei) {
            int source = boost::source(*ei,mission_decomposition);
            int target = boost::target(*ei,mission_decomposition);
            auto edge = boost::edge(source,target,mission_decomposition).first;

            int children_num = 0;
            ATGraph::out_edge_iterator target_ei, target_ei_end;
            for(boost::tie(target_ei,target_ei_end) = out_edges(target,mission_decomposition);target_ei != target_ei_end;++target_ei) {
                children_num++;
            }

            if(mission_decomposition[target].node_type == GOALNODE || (mission_decomposition[target].node_type == OP && children_num < 2)) {
                bool goal_node = true;
                int child_id = target;
                while(goal_node) {
                    ATGraph::out_edge_iterator ci, ci_end;
                    for(boost::tie(ci,ci_end) = out_edges(child_id,mission_decomposition);ci != ci_end;++ci) {
                        int s = boost::source(*ci,mission_decomposition);
                        int t = boost::target(*ci,mission_decomposition);
                        auto e = boost::edge(s,t,mission_decomposition).first;

                        /*
                            A goal node only has a goal as child if it has a Means-end decomposition
                        */
                        if(mission_decomposition[t].node_type != GOALNODE) {
                            goal_node = false;
                            if(mission_decomposition[e].edge_type == NORMALAND || mission_decomposition[e].edge_type == NORMALOR) {
                                if(t == next_node.first) {
                                    is_child = true;
                                    break;
                                }
                            }
                        } else {
                            child_id = t;
                        }
                    }
                }

                if(is_child) {
                    break;
                }
            } else {
                if(mission_decomposition[edge].edge_type == NORMALAND || mission_decomposition[edge].edge_type == NORMALOR) {
                    if(target == next_node.first) {
                        is_child = true;
                        break;
                    }
                }
            }
        }

        if(is_child) {
            map<int,pair<vector<variant<ground_literal,pair<ground_literal,variant<int,float>>>>,vector<pair<literal,vector<string>>>>> aux = recursive_valid_mission_decomposition(";", mission_queue, depth, children_effects);
            
            map<int,pair<vector<variant<ground_literal,pair<ground_literal,variant<int,float>>>>,vector<pair<literal,vector<string>>>>>::iterator ceff_it;
            for(ceff_it = aux.begin(); ceff_it != aux.end(); ++ceff_it) {
                if(children_effects.find(ceff_it->first) == children_effects.end()) {
                    children_effects[ceff_it->first] = ceff_it->second;
                } else {
                    vector<variant<ground_literal,pair<ground_literal,variant<int,float>>>> tmp = children_effects[ceff_it->first].first;
                    tmp.insert(tmp.end(), ceff_it->second.first.begin(), ceff_it->second.first.end());

                    children_effects[ceff_it->first].first = tmp;

                    vector<pair<literal,vector<string>>> tmp2 = children_effects[ceff_it->first].second;
                    tmp2.insert(tmp2.end(), ceff_it->second.second.begin(), ceff_it->second.second.end());

                    children_effects[ceff_it->first].second = tmp2;
                }
            }
        } else {
            checking_children = false;
        }
    }
}

/*
    Function: check_parallel_op_children
    Objective: Check children of a parallel operator when generating valid mission decompositions.
    Functioning: The basic functioning is:
        -> Go through the queue while the next node in the queue is a child of this operator
            - This is done checking the out edges of the parallel operator node and verifying if the node in the queue is present
        -> For each child we recursively perform decomposition
            - Effects are kept in a children effects map, where they are applied after finishing all of the executions
    
    @ Input 1: A reference to the mission queue
    @ Input 2: A reference to the children effects map
    @ Input 3: The current node depth
    @ Inpui 4: The current node info
    @ Output: Void. The recursive call to the valid missions generation function is performed for each child.
*/
void ValidMissionGenerator::check_parallel_op_children(queue<pair<int,ATNode>>& mission_queue, map<int,pair<vector<variant<ground_literal,pair<ground_literal,variant<int,float>>>>,vector<pair<literal,vector<string>>>>>& children_effects, int depth, pair<int,ATNode> current_node) {
    bool checking_children = true;
    bool is_or = false;

    vector<pair<vector<pair<int,ATNode>>,set<int>>> initial_decompositions = valid_mission_decompositions;

    vector<vector<pair<vector<pair<int,ATNode>>,set<int>>>> child_decompositions;

    while(checking_children) {
        if(mission_queue.size() == 0) {
            break;
        }
        pair<int,ATNode> next_node = mission_queue.front();

        bool is_child = false;

        ATGraph::out_edge_iterator ei, ei_end;
        for(boost::tie(ei,ei_end) = out_edges(current_node.first,mission_decomposition);ei != ei_end;++ei) {
            int source = boost::source(*ei,mission_decomposition);
            int target = boost::target(*ei,mission_decomposition);
            auto edge = boost::edge(source,target,mission_decomposition).first;

            if(mission_decomposition[edge].edge_type == NORMALOR) {
                is_or = true;
            }

            int children_num = 0;
            ATGraph::out_edge_iterator target_ei, target_ei_end;
            for(boost::tie(target_ei,target_ei_end) = out_edges(target,mission_decomposition);target_ei != target_ei_end;++target_ei) {
                children_num++;
            }

            /*
                If we have a goal node as child we have to search its children for the next node
            */
            if(mission_decomposition[target].node_type == GOALNODE || (mission_decomposition[target].node_type == OP && children_num < 2)) {
                bool goal_node = true;
                int child_id = target;
                while(goal_node) {
                    ATGraph::out_edge_iterator ci, ci_end;
                    for(boost::tie(ci,ci_end) = out_edges(child_id,mission_decomposition);ci != ci_end;++ci) {
                        int s = boost::source(*ci,mission_decomposition);
                        int t = boost::target(*ci,mission_decomposition);
                        auto e = boost::edge(s,t,mission_decomposition).first;

                        /*
                            A goal node only has a goal as child if it has a Means-end decomposition
                        */
                        if(mission_decomposition[t].node_type != GOALNODE) {
                            goal_node = false;
                            if(mission_decomposition[e].edge_type == NORMALAND || mission_decomposition[e].edge_type == NORMALOR) {
                                if(t == next_node.first) {
                                    is_child = true;
                                    break;
                                }
                            }
                        } else {
                            child_id = t;
                        }
                    }
                }

                if(is_child) {
                    break;
                }
            } else {
                if(mission_decomposition[edge].edge_type == NORMALAND || mission_decomposition[edge].edge_type == NORMALOR) {
                    if(target == next_node.first) {
                        is_child = true;
                        break;
                    }
                }
            }
        }

        if(is_child) {
            if(!is_or) {
                map<int,pair<vector<variant<ground_literal,pair<ground_literal,variant<int,float>>>>,vector<pair<literal,vector<string>>>>> child_effects;
                child_effects = recursive_valid_mission_decomposition("#", mission_queue, depth, child_effects);

                map<int,pair<vector<variant<ground_literal,pair<ground_literal,variant<int,float>>>>,vector<pair<literal,vector<string>>>>>::iterator ceff_it;
                for(ceff_it = child_effects.begin(); ceff_it != child_effects.end(); ++ceff_it) {
                    if(children_effects.find(ceff_it->first) == children_effects.end()) {
                        children_effects[ceff_it->first] = ceff_it->second;
                    } else {
                        vector<variant<ground_literal,pair<ground_literal,variant<int,float>>>> aux = children_effects[ceff_it->first].first;
                        aux.insert(aux.end(), ceff_it->second.first.begin(), ceff_it->second.first.end());

                        children_effects[ceff_it->first].first = aux;

                        vector<pair<literal,vector<string>>> aux2 = children_effects[ceff_it->first].second;
                        aux2.insert(aux2.end(), ceff_it->second.second.begin(), ceff_it->second.second.end());

                        children_effects[ceff_it->first].second = aux2;
                    }
                }
                
                child_effects.clear();
            } else {
                /*
                    Here is where we deal with an OR decomposed node

                    -> We need to keep the valid mission decompositions vector we have at the moment we reach this node
                        - For each child, the valid mission decompositions vector will be reset to this vector
                        - A valid mission decompositions vector will be kept for each child
                        - At the end, all of the children valid mission decompositions vector will be appended to form the global valid mission decompositions vector
                    
                    -> How to deal with the child_effects and children_effects?
                        - Possibly nothing will need to be done
                        - The key in these maps is the ID of the node, which solves any problem related to conflicts
                */
                valid_mission_decompositions = initial_decompositions;

                map<int,pair<vector<variant<ground_literal,pair<ground_literal,variant<int,float>>>>,vector<pair<literal,vector<string>>>>> child_effects;
                child_effects = recursive_valid_mission_decomposition("#", mission_queue, depth, child_effects);

                map<int,pair<vector<variant<ground_literal,pair<ground_literal,variant<int,float>>>>,vector<pair<literal,vector<string>>>>>::iterator ceff_it;
                for(ceff_it = child_effects.begin(); ceff_it != child_effects.end(); ++ceff_it) {
                    if(children_effects.find(ceff_it->first) == children_effects.end()) {
                        children_effects[ceff_it->first] = ceff_it->second;
                    } else {
                        vector<variant<ground_literal,pair<ground_literal,variant<int,float>>>> aux = children_effects[ceff_it->first].first;
                        aux.insert(aux.end(), ceff_it->second.first.begin(), ceff_it->second.first.end());

                        children_effects[ceff_it->first].first = aux;

                        vector<pair<literal,vector<string>>> aux2 = children_effects[ceff_it->first].second;
                        aux2.insert(aux2.end(), ceff_it->second.second.begin(), ceff_it->second.second.end());

                        children_effects[ceff_it->first].second = aux2;
                    }
                }
                
                child_effects.clear();

                child_decompositions.push_back(valid_mission_decompositions);
            }
        } else {
            checking_children = false;
        }
    }

    if(is_or) {
        valid_mission_decompositions.clear();
        for(auto decomposition : child_decompositions) {
            valid_mission_decompositions.insert(valid_mission_decompositions.end(), decomposition.begin(), decomposition.end());
        }
    }

    solve_conflicts(children_effects);
}

void ValidMissionGenerator::check_fallback_op_children(queue<pair<int,ATNode>>& mission_queue, map<int,pair<vector<variant<ground_literal,pair<ground_literal,variant<int,float>>>>,vector<pair<literal,vector<string>>>>>& children_effects, int depth, pair<int,ATNode> current_node) {
    bool checking_children = true;

    vector<pair<vector<pair<int,ATNode>>,set<int>>> initial_decompositions = valid_mission_decompositions;

    vector<vector<pair<vector<pair<int,ATNode>>,set<int>>>> child_decompositions;
    
    while(checking_children) {
        if(mission_queue.size() == 0) {
            break;
        }
        pair<int,ATNode> next_node = mission_queue.front();

        bool is_child = false;

        ATGraph::out_edge_iterator ei, ei_end;
        for(boost::tie(ei,ei_end) = out_edges(current_node.first,mission_decomposition);ei != ei_end;++ei) {
            int source = boost::source(*ei,mission_decomposition);
            int target = boost::target(*ei,mission_decomposition);
            auto edge = boost::edge(source,target,mission_decomposition).first;

            int children_num = 0;
            ATGraph::out_edge_iterator target_ei, target_ei_end;
            for(boost::tie(target_ei,target_ei_end) = out_edges(target,mission_decomposition);target_ei != target_ei_end;++target_ei) {
                children_num++;
            }

            /*
                If we have a goal node as child we have to search its children for the next node
            */
            if(mission_decomposition[target].node_type == GOALNODE || (mission_decomposition[target].node_type == OP && children_num < 2)) {
                bool goal_node = true;
                int child_id = target;
                while(goal_node) {
                    ATGraph::out_edge_iterator ci, ci_end;
                    for(boost::tie(ci,ci_end) = out_edges(child_id,mission_decomposition);ci != ci_end;++ci) {
                        int s = boost::source(*ci,mission_decomposition);
                        int t = boost::target(*ci,mission_decomposition);
                        auto e = boost::edge(s,t,mission_decomposition).first;

                        /*
                            A goal node only has a goal as child if it has a Means-end decomposition
                        */
                        if(mission_decomposition[t].node_type != GOALNODE) {
                            goal_node = false;
                            if(mission_decomposition[e].edge_type == NORMALAND || mission_decomposition[e].edge_type == NORMALOR) {
                                if(t == next_node.first) {
                                    is_child = true;
                                    break;
                                }
                            }
                        } else {
                            child_id = t;
                        }
                    }
                }

                if(is_child) {
                    break;
                }
            } else {
                if(mission_decomposition[edge].edge_type == NORMALAND || mission_decomposition[edge].edge_type == NORMALOR) {
                    if(target == next_node.first) {
                        is_child = true;
                        break;
                    }
                }
            }
        }

        if(is_child) {
            map<int,pair<vector<variant<ground_literal,pair<ground_literal,variant<int,float>>>>,vector<pair<literal,vector<string>>>>> child_effects;
            child_effects = recursive_valid_mission_decomposition("#", mission_queue, depth, child_effects);

            map<int,pair<vector<variant<ground_literal,pair<ground_literal,variant<int,float>>>>,vector<pair<literal,vector<string>>>>>::iterator ceff_it;
            for(ceff_it = child_effects.begin(); ceff_it != child_effects.end(); ++ceff_it) {
                if(children_effects.find(ceff_it->first) == children_effects.end()) {
                    children_effects[ceff_it->first] = ceff_it->second;
                } else {
                    vector<variant<ground_literal,pair<ground_literal,variant<int,float>>>> aux = children_effects[ceff_it->first].first;
                    aux.insert(aux.end(), ceff_it->second.first.begin(), ceff_it->second.first.end());

                    children_effects[ceff_it->first].first = aux;

                    vector<pair<literal,vector<string>>> aux2 = children_effects[ceff_it->first].second;
                    aux2.insert(aux2.end(), ceff_it->second.second.begin(), ceff_it->second.second.end());

                    children_effects[ceff_it->first].second = aux2;
                }
            }
                
            child_effects.clear();
        } else {
            checking_children = false;
        }
    }
}

/*
    Function: generate_mission_queue
    Objective: Generate the mission queue based on the Task Graph

    @ Output: The generated mission queue
*/
queue<pair<int,ATNode>> ValidMissionGenerator::generate_mission_queue() {
    auto nodes = vertices(mission_decomposition);

    int graph_size = *nodes.second - *nodes.first;

    /*
        Go through the graph in a DFS order and put nodes in a queue

        -> Goal Nodes are not considered
        -> Operator nodes with only one child are not considered
    */
    queue<pair<int,ATNode>> mission_queue;

    //Populate the mission queue
    if(!is_unique_branch(mission_decomposition)) {
        for(int i = 0; i < graph_size; i++) {
            if(mission_decomposition[i].node_type == ATASK) {
                mission_queue.push(make_pair(i,mission_decomposition[i]));
            } else if(mission_decomposition[i].node_type == OP) {
                int out_edge_num = 0;
                ATGraph::out_edge_iterator ei, ei_end;

                //Only insert OP nodes that have more than one outer edge of normal type (more than one child)
                for(boost::tie(ei,ei_end) = out_edges(i,mission_decomposition);ei != ei_end;++ei) {
                    auto source = boost::source(*ei,mission_decomposition);
                    auto target = boost::target(*ei,mission_decomposition);
                    auto edge = boost::edge(source,target,mission_decomposition);

                    if(mission_decomposition[edge.first].edge_type == NORMALAND || mission_decomposition[edge.first].edge_type == NORMALOR) {
                        out_edge_num++;
                    }
                }

                if(out_edge_num > 1) {
                    mission_queue.push(make_pair(i,mission_decomposition[i]));
                }
            }
        }
    } else {
        auto indexmap = boost::get(boost::vertex_index, mission_decomposition);
		auto colormap = boost::make_vector_property_map<boost::default_color_type>(indexmap);

		DFSATVisitor vis;
		boost::depth_first_search(mission_decomposition, vis, colormap, 0);

		vector<int> vctr = vis.GetVector();

        for(int i : vctr) {
            if(mission_decomposition[i].is_achieve_type && mission_decomposition[i].node_type != GOALNODE) {
                mission_queue.push(make_pair(i,mission_decomposition[i]));
            }

            if(mission_decomposition[i].node_type == ATASK) {
                if(mission_queue.size() == 0) {
                    mission_queue.push(make_pair(0,mission_decomposition[0]));
                }

                mission_queue.push(make_pair(i,mission_decomposition[i]));
            }
        }
    }

    if(verbose) {
        queue<pair<int,ATNode>> queue_copy = mission_queue;
        std::cout << "Mission Queue" << std::endl;
        while(!queue_copy.empty()) {
            pair<int,ATNode> node = queue_copy.front();
            queue_copy.pop(); 
            if(node.second.node_type == ATASK) {
                std::cout << "ATASK: " << get<AbstractTask>(node.second.content).id << " - " << get<AbstractTask>(node.second.content).name << std::endl;
            } else {
                std::cout << "OPERATOR: " << get<string>(node.second.content) << std::endl;
            }
        }
    }

    return mission_queue;
}

/*
    Function: check_conditions
    Objective: Apply the effects that were left to apply and remove decompositions that do not satisfy achieve conditions

    @ Input 1: A reference to the map of effects to be applied
    @ Input 2: The current node to be evaluated
    @ Output: Void. The valid mission decompositions will be trimmed (if needed)
*/
void ValidMissionGenerator::check_conditions(map<int,pair<vector<variant<ground_literal,pair<ground_literal,variant<int,float>>>>,vector<pair<literal,vector<string>>>>> effects_to_apply, pair<int,ATNode> current_node) {
    if(current_node.second.is_achieve_type) {
        string achievel_goal_id;

        if(current_node.second.node_type != GOALNODE) {
            ATGraph::out_edge_iterator ei, ei_end;

            for(boost::tie(ei,ei_end) = out_edges(current_node.first,mission_decomposition);ei != ei_end;++ei) {
                auto source = boost::source(*ei,mission_decomposition);
                auto target = boost::target(*ei,mission_decomposition);
                auto edge = boost::edge(source,target,mission_decomposition);

                if(mission_decomposition[edge.first].edge_type == NORMALAND || mission_decomposition[edge.first].edge_type == NORMALOR) {
                    if(mission_decomposition[target].node_type == GOALNODE) {
                        achievel_goal_id = std::get<string>(mission_decomposition[target].content);
                        break;
                    }
                }
            }
        } else {
            achievel_goal_id = std::get<string>(current_node.second.content);
        }

        int gm_node_id = find_gm_node_by_id(achievel_goal_id, gm); 

        AchieveCondition achieve_condition = get<AchieveCondition>(gm[gm_node_id].custom_props[achieve_condition_prop]);

        vector<ground_literal> achieve_condition_predicates;
        vector<pair<ground_literal,variant<pair<bool,variant<int,float>>,pair<string,variant<int,float>>>>> achieve_condition_func_predicates;

        variant<pair<pair<predicate_definition,vector<string>>,bool>,pair<pair<predicate_definition,vector<string>>,pair<variant<int,float>,variant<bool,string>>>,bool> evaluation = achieve_condition.evaluate_condition(semantic_mapping, gm_var_map);

        bool need_predicate_checking = false;
        bool need_function_predicate_checking = false;
        if(holds_alternative<pair<pair<predicate_definition,vector<string>>,bool>>(evaluation)) {
            pair<pair<predicate_definition,vector<string>>,bool> eval = std::get<pair<pair<predicate_definition,vector<string>>,bool>>(evaluation);

            for(string value : eval.first.second) {
                ground_literal aux;

                aux.predicate = eval.first.first.name;
                aux.positive = !eval.second;
                aux.args.push_back(value);

                achieve_condition_predicates.push_back(aux);
            } 

            need_predicate_checking = true;
        } else if(holds_alternative<pair<pair<predicate_definition,vector<string>>,pair<variant<int,float>,variant<bool,string>>>>(evaluation)) {
            pair<pair<predicate_definition,vector<string>>,pair<variant<int,float>,variant<bool,string>>> eval = std::get<pair<pair<predicate_definition,vector<string>>,pair<variant<int,float>,variant<bool,string>>>>(evaluation);

            for(string value : eval.first.second) {
                ground_literal aux;

                aux.predicate = eval.first.first.name;
                aux.args.push_back(value);

                if(holds_alternative<bool>(eval.second.second)) {
                    pair<bool,variant<int,float>> val_and_flag = make_pair(std::get<bool>(eval.second.second), eval.second.first);

                    achieve_condition_func_predicates.push_back(make_pair(aux,val_and_flag));
                } else {
                    pair<string,variant<int,float>> op_and_val = make_pair(std::get<string>(eval.second.second), eval.second.first);

                    achieve_condition_func_predicates.push_back(make_pair(aux,op_and_val));
                }
            }

            need_function_predicate_checking = true;
        }

        vector<int> decompositions_to_erase;
        if(need_predicate_checking) {  
            map<int,std::vector<ground_literal>> pred_effs;

            map<int,pair<vector<variant<ground_literal,pair<ground_literal,variant<int,float>>>>,vector<pair<literal,vector<string>>>>>::iterator eff_it;
            for(eff_it = effects_to_apply.begin(); eff_it != effects_to_apply.end(); ++eff_it) {
                for(auto eff : eff_it->second.first) {
                    if(holds_alternative<ground_literal>(eff)) {
                        pred_effs[eff_it->first].push_back(std::get<ground_literal>(eff));
                    }
                }
            }

            int decomposition_index = 0;
            for(auto decomposition : valid_mission_decompositions) {
                vector<ground_literal> ws = apply_pred_effects(pred_effs, decomposition.second);

                bool valid_achieve_condition = true;
                for(ground_literal forAll_pred : achieve_condition_predicates) {
                    for(ground_literal state : ws) {
                        bool same_predicate = is_same_predicate(state, forAll_pred);
                              
                        if(!same_predicate) {
                            break;
                        }

                        if(state.positive != forAll_pred.positive) {
                            valid_achieve_condition = false;
                            break;
                        }
                    }

                    if(!valid_achieve_condition) {
                        break;
                    }
                }

                if(!valid_achieve_condition) {
                    decompositions_to_erase.push_back(decomposition_index);
                }

                decomposition_index++;
            }
        } else if(need_function_predicate_checking) {
            map<int,vector<pair<ground_literal,variant<int,float>>>> func_effs;

            map<int,pair<vector<variant<ground_literal,pair<ground_literal,variant<int,float>>>>,vector<pair<literal,vector<string>>>>>::iterator eff_it;
            for(eff_it = effects_to_apply.begin(); eff_it != effects_to_apply.end(); ++eff_it) {
                for(auto eff : eff_it->second.first) {
                    if(holds_alternative<pair<ground_literal,variant<int,float>>>(eff)) {
                        func_effs[eff_it->first].push_back(std::get<pair<ground_literal,variant<int,float>>>(eff));
                    }
                }
            }

            int decomposition_index = 0;
            for(auto decomposition : valid_mission_decompositions) {
                vector<pair<ground_literal,variant<int,float>>> wsf = apply_func_effects(func_effs, decomposition.second);

                bool valid_achieve_condition = true;
                for(pair<ground_literal,variant<pair<bool,variant<int,float>>,pair<string,variant<int,float>>>> forAll_pred : achieve_condition_func_predicates) {
                    for(pair<ground_literal,variant<int,float>> state : wsf) {
                        bool same_predicate = is_same_predicate(state.first, forAll_pred.first);
                                        
                        if(!same_predicate) {
                            break;
                        }

                        if(holds_alternative<pair<bool,variant<int,float>>>(forAll_pred.second)) {
                            pair<bool,variant<int,float>> val_and_flag = std::get<pair<bool,variant<int,float>>>(forAll_pred.second);
                            if(val_and_flag.first) {
                                if(holds_alternative<int>(state.second)) {
                                    int state_val = std::get<int>(state.second);

                                    if(holds_alternative<int>(val_and_flag.second)) {
                                        valid_achieve_condition = (state_val != std::get<int>(val_and_flag.second));
                                    } else {
                                        valid_achieve_condition = !compare_int_and_float(state_val, std::get<float>(val_and_flag.second));
                                    }
                                } else {
                                    float state_val = std::get<float>(state.second);

                                    if(holds_alternative<int>(val_and_flag.second)) {
                                        valid_achieve_condition = !compare_int_and_float(std::get<int>(val_and_flag.second), state_val);
                                    } else {
                                        valid_achieve_condition = !compare_floats(state_val, std::get<float>(val_and_flag.second));
                                    }
                                }

                                if(!valid_achieve_condition) {
                                    break;
                                }
                            } else {
                                if(holds_alternative<int>(state.second)) {
                                    int state_val = std::get<int>(state.second);

                                    if(holds_alternative<int>(val_and_flag.second)) {
                                        valid_achieve_condition = (state_val == std::get<int>(val_and_flag.second));
                                    } else {
                                        valid_achieve_condition = compare_int_and_float(state_val, std::get<float>(val_and_flag.second));
                                    }
                                } else {
                                    float state_val = std::get<float>(state.second);

                                    if(holds_alternative<int>(val_and_flag.second)) {
                                        valid_achieve_condition = compare_int_and_float(std::get<int>(val_and_flag.second), state_val);
                                    } else {
                                        valid_achieve_condition = compare_floats(state_val, std::get<float>(val_and_flag.second));
                                    }
                                }
                            }
                        } else {
                            pair<string,variant<int,float>> op_and_val = std::get<pair<string,variant<int,float>>>(forAll_pred.second);

                            if(holds_alternative<int>(state.second)) {
                                int state_val = std::get<int>(state.second);

                                if(holds_alternative<int>(op_and_val.second)) {
                                    if(op_and_val.first == ocl_gt) {
                                        valid_achieve_condition = state_val > std::get<int>(op_and_val.second);
                                    } else if(op_and_val.first == ocl_lt) {
                                        valid_achieve_condition = state_val < std::get<int>(op_and_val.second);
                                    } else if(op_and_val.first == ocl_geq) {
                                        valid_achieve_condition = state_val >= std::get<int>(op_and_val.second);
                                    } else if(op_and_val.first == ocl_leq) {
                                        valid_achieve_condition = state_val <= std::get<int>(op_and_val.second);
                                    }
                                } else {
                                    if(op_and_val.first == ocl_gt) {
                                        valid_achieve_condition = greater_than_int_and_float(state_val, std::get<float>(op_and_val.second));
                                    } else if(op_and_val.first == ocl_lt) {
                                        valid_achieve_condition = greater_than_float_and_int(state_val, std::get<float>(op_and_val.second));
                                    } else if(op_and_val.first == ocl_geq) {
                                        valid_achieve_condition = !greater_than_float_and_int(state_val, std::get<float>(op_and_val.second));
                                    } else if(op_and_val.first == ocl_leq) {
                                        valid_achieve_condition = !greater_than_int_and_float(state_val, std::get<float>(op_and_val.second));
                                    }
                                }
                            } else {
                                float state_val = std::get<float>(state.second);

                                if(holds_alternative<int>(op_and_val.second)) {
                                    if(op_and_val.first == ocl_gt) {
                                        valid_achieve_condition = greater_than_float_and_int(std::get<int>(op_and_val.second), state_val);
                                    } else if(op_and_val.first == ocl_lt) {
                                        valid_achieve_condition = greater_than_int_and_float(std::get<int>(op_and_val.second), state_val);
                                    } else if(op_and_val.first == ocl_geq) {
                                        valid_achieve_condition = !greater_than_int_and_float(std::get<int>(op_and_val.second), state_val);
                                    } else if(op_and_val.first == ocl_leq) {
                                        valid_achieve_condition = !greater_than_float_and_int(std::get<int>(op_and_val.second), state_val);
                                    }
                                } else {
                                    if(op_and_val.first == ocl_gt) {
                                        valid_achieve_condition = greater_than_floats(state_val, std::get<float>(op_and_val.second));
                                    } else if(op_and_val.first == ocl_lt) {
                                        valid_achieve_condition = greater_than_floats(std::get<float>(op_and_val.second), state_val);
                                    } else if(op_and_val.first == ocl_geq) {
                                        valid_achieve_condition = !greater_than_floats(std::get<float>(op_and_val.second), state_val);
                                    } else if(op_and_val.first == ocl_leq) {
                                        valid_achieve_condition = !greater_than_floats(state_val, std::get<float>(op_and_val.second));
                                    }
                                }
                            }

                            if(!valid_achieve_condition) break;
                        }
                    }

                    if(!valid_achieve_condition) {
                        break;
                    }
                }

                if(!valid_achieve_condition) {
                    decompositions_to_erase.push_back(decomposition_index);
                }

                decomposition_index++;
            }
        }

        std::sort(decompositions_to_erase.begin(), decompositions_to_erase.end());
        for(auto i = decompositions_to_erase.rbegin(); i != decompositions_to_erase.rend(); ++i) {
            valid_mission_decompositions.erase(valid_mission_decompositions.begin() + *i);
        }

        if(valid_mission_decompositions.size() == 0) {
            string forAll_condition_not_achieved = "No decomposition satisfied achieve condition of goal " + achievel_goal_id;

            throw std::runtime_error(forAll_condition_not_achieved);
        }
    }
}

vector<ground_literal> ValidMissionGenerator::apply_pred_effects(map<int,vector<ground_literal>> pred_eff, set<int> tasks_to_consider) {
    vector<ground_literal> ws = world_state;

    map<int,vector<ground_literal>>::iterator pred_eff_it;
    for(pred_eff_it = pred_eff.begin(); pred_eff_it != pred_eff.end(); ++pred_eff_it) {
        if(tasks_to_consider.find(pred_eff_it->first) != tasks_to_consider.end()) {
            for(ground_literal eff : pred_eff_it->second) {
                bool found_pred = false;
                for(ground_literal& state : ws) {
                    bool same_predicate = is_same_predicate(state, eff);

                    if(same_predicate) {
                        if(state.positive != eff.positive) {
                            state.positive = eff.positive;
                        }

                        found_pred = true;
                        break;
                    }
                }

                if(!found_pred) {
                    ws.push_back(eff);
                }
            }
        }
    }

    return ws;
}

void ValidMissionGenerator::solve_conflicts(map<int,pair<vector<variant<ground_literal,pair<ground_literal,variant<int,float>>>>,vector<pair<literal,vector<string>>>>> children_effects) {
    /*
        -> Verify in the children effects if we have any conflicts for each mission decomposition
            - If there is, erase the decomposition
            - Conflicts onyl happen in predicates effects
    */
    vector<int> decompositions_to_erase;

    int decomposition_index = 0;
    for(auto decomposition : valid_mission_decompositions) {
        vector<ground_literal> applied_effs;

        bool found_conflict = false;
        
        map<int,pair<vector<variant<ground_literal,pair<ground_literal,variant<int,float>>>>,vector<pair<literal,vector<string>>>>>::iterator ceff_it;
        for(ceff_it = children_effects.begin(); ceff_it != children_effects.end(); ++ceff_it) {
            if(decomposition.second.find(ceff_it->first) != decomposition.second.end()) {
                for(auto eff : ceff_it->second.first) {
                    if(holds_alternative<ground_literal>(eff)) {
                        ground_literal e = std::get<ground_literal>(eff);
                        
                        for(ground_literal a_eff : applied_effs) {
                            bool same_predicate = is_same_predicate(e, a_eff);

                            if(same_predicate) {
                                if(e.positive != a_eff.positive) {
                                    found_conflict = true;

                                    break;
                                }
                            }
                        }

                        if(!found_conflict) {
                            applied_effs.push_back(e);
                        }
                    }
                }
            }

            if(found_conflict) break;
        }

        if(found_conflict) {
            decompositions_to_erase.push_back(decomposition_index);
        }

        decomposition_index++;
    }

    std::sort(decompositions_to_erase.begin(), decompositions_to_erase.end());
    for(auto i = decompositions_to_erase.rbegin(); i != decompositions_to_erase.rend(); ++i) {
        valid_mission_decompositions.erase(valid_mission_decompositions.begin() + *i);
    }

    if(valid_mission_decompositions.size() == 0) {
        string conflict_solving_error = "Could not solve conflicts in valid mission decompositions generation";

        throw std::runtime_error(conflict_solving_error);
    }
}

bool ValidMissionGenerator::check_preconditions_for_decomposition(pair<int,ATNode> task_decomposition, pair<vector<pair<int,ATNode>>,set<int>> valid_mission_decomposition, Decomposition& d, vector<ground_literal> ws, 
                                                                    vector<pair<ground_literal,variant<int,float>>> wsf, map<int,vector<pair<literal,vector<string>>>> ws_ng) {
    bool preconditions_hold = check_decomposition_preconditions(ws, wsf, ws_ng, make_pair(task_decomposition.first,d), mission_constraints, valid_mission_decomposition.second, robot_related_sorts);

    /*
        Check for any context dependency in current decomposition. If a valid mission decomposition does not contain any tasks
        involved in this kind of dependency, preconditions do not hold.
    */
    if(preconditions_hold) {
        Decomposition aux = d;
        expand_decomposition(d, wsf, verbose);
        if(aux.path.decomposition.size() != d.path.decomposition.size()) {
            expanded_decompositions.insert(d);
        }

        ATGraph::in_edge_iterator iei, ied;

        for(boost::tie(iei,ied) = boost::in_edges(task_decomposition.first,mission_decomposition); iei != ied; ++iei) {
            bool found_cdepend_node = false;
            bool has_cdependency = false;

            int source = boost::source(*iei,mission_decomposition);
            int target = boost::target(*iei,mission_decomposition);
            auto edge = boost::edge(source,target,mission_decomposition).first;

            ATEdge e = mission_decomposition[edge];
            if(e.edge_type == CDEPEND) {
                has_cdependency = true;
                for(pair<int,ATNode> node : valid_mission_decomposition.first) {
                    if(node.first == source) {
                        found_cdepend_node = true;
                        break;
                    }
                }
            }

            if(has_cdependency) {
                if(!found_cdepend_node) {
                    preconditions_hold = false;
                    break;
                }
            }
        }
    }

    return preconditions_hold;
}

vector<pair<ground_literal,variant<int,float>>> ValidMissionGenerator::apply_func_effects(map<int,vector<pair<ground_literal,variant<int,float>>>> func_eff, set<int> tasks_to_consider) {
    vector<pair<ground_literal,variant<int,float>>> wsf = world_state_functions;

    map<int,vector<pair<ground_literal,variant<int,float>>>>::iterator func_eff_it;
    for(func_eff_it = func_eff.begin(); func_eff_it != func_eff.end(); ++func_eff_it) {
        if(tasks_to_consider.find(func_eff_it->first) != tasks_to_consider.end()) {
            for(pair<ground_literal,variant<int,float>> eff : func_eff_it->second) {
                bool found_pred = false;
                for(pair<ground_literal,variant<int,float>>& state : wsf) {
                    bool same_predicate = is_same_predicate(state.first, eff.first);

                    if(same_predicate) {
                        if(eff.first.isAssignCostChange) {
                            state.second = eff.second;
                        } else {
                            if(holds_alternative<int>(state.second)) {
                                int state_val = std::get<int>(state.second);

                                if(holds_alternative<int>(eff.second)) {
                                    state.second = state_val + std::get<int>(eff.second);
                                } else {
                                    state.second = static_cast<float>(state_val) + std::get<float>(eff.second);
                                }
                            } else {
                                float state_val = std::get<float>(state.second);

                                if(holds_alternative<int>(eff.second)) {
                                    state.second = state_val + static_cast<float>(std::get<int>(eff.second));
                                } else {
                                    state.second = state_val + std::get<float>(eff.second);
                                }
                            }
                        }
                    
                        found_pred = true;
                        break;
                    }
                }

                if(!found_pred) {
                    wsf.push_back(eff);
                }
            }
        }
    }

    return wsf;
}