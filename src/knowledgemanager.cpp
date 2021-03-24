#include "knowledgemanager.hpp"
#include <sstream>
#include <iostream>

#include <boost/foreach.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/property_tree/xml_parser.hpp>

/*
    Function: construct_knowledge_base
    Objective: Construct a knowledge base given the database name and the configuration map

    @ Input 1: The database name
    @ Input 2: The configuration map, obtained from the parsing of the configuration file
    @ Output: The constructed KnowledgeBase object

    NOTES: -> For now, the only type allowed is XML file
		   -> When (and if) more types are allowed we need to delegate the task of opening these files to functions
		    passing the configuration file as a function parameter
*/ 
KnowledgeBase construct_knowledge_base(string db_name, map<string, variant<map<string,string>, vector<string>, vector<SemanticMapping>, vector<VariableMapping>, pair<string,string>>> cfg) {
    string db_type = get<map<string,string>>(cfg[db_name])["type"];

    pt::ptree db_knowledge;
    string db_root = "";
	if(db_type == "file") {
		string db_file_type = get<map<string,string>>(cfg[db_name])["file_type"];
		if(db_file_type == "xml") {
			pt::read_xml(get<map<string,string>>(cfg[db_name])["path"], db_knowledge);
            
            db_root = get<map<string,string>>(cfg[db_name])["xml_root"];
		}
	}

    KnowledgeBase kb(db_name, db_knowledge, db_root);

    return kb;
}

/*
    Function: initialize_objects
    Objective: Initalize the objects on the sorts map based on the knowledge bases (world and robot)

    @ Input 1: World knowledge database
    @ Input 2: Robots database
    @ Input 3: The reference to the sorts map
    @ Input 4: The high-level location type (for now it is only one)
    @ Input 5: The map of abstract task instances
    @ Input 6: The mapping between HDDL types and OCL types
    @ Output: Void. The sorts are initialized
*/ 
void initialize_objects(KnowledgeBase worlddb, KnowledgeBase robotsdb, map<string,set<string>>& sorts, string location_type,
                            map<string,vector<AbstractTask>>& at_instances, map<string,string> type_mapping) {

    pt::ptree worlddb_root;
    if(worlddb.get_root_key() == "") {
        worlddb_root = worlddb.get_knowledge();
    } else {
        worlddb_root = worlddb.get_knowledge().get_child(worlddb.get_root_key());
    }

    pt::ptree robotsdb_root;
    if(robotsdb.get_root_key() == "") {
        robotsdb_root = robotsdb.get_knowledge();
    } else {
        robotsdb_root = robotsdb.get_knowledge().get_child(robotsdb.get_root_key());
    }
    /*
        All the robots considered for the mission are present in the assignments file. In order to add them as constants we need to check
        if they have types.

        -> If a robot does not have a type we put it as native robot type
        -> Robot locations are given as robotlocation type
    */
    bool has_object_type = false;
    if(sorts["object"].size() > 0) {    
        has_object_type = true;
    }
    
    /*
        Here we will add locations and robots. For locations we need to be careful: we need to check what is the type of the location!

        -> For now this means that we need to check if that location is of location_type and present in the World model
        -> If it isn't, it will be added as default location type
    */
    set<string> world_locations; //Locations that must be declared in the DSL

	BOOST_FOREACH(pt::ptree::value_type& child, worlddb_root) {
		if(child.first == location_type) {
			world_locations.insert(child.second.get<string>("name"));
            string hddl_type;

            hddl_type = type_mapping[location_type];
            sorts[hddl_type].insert(child.second.get<string>("name"));
		}
	}

    map<string,vector<string>> robot_types;

    BOOST_FOREACH(pt::ptree::value_type child, robotsdb_root) {
        if(child.first == "robot") {
            Robot r;
            r.name = child.second.get<string>("name");
            r.type = child.second.get<string>("type");
            r.pos = child.second.get<string>("pos");

            assigned_robots.push_back(r);

            if(has_object_type) {
                sorts["object"].insert(r.name);
                sorts["object"].insert(r.pos);
            }
                            
            sorts["robot"].insert(r.name);
            if(sorts.find(r.type) != sorts.end()) { //The robot type is present in sorts
                sorts[r.type].insert(r.name);
            }

            sorts["robotlocation"].insert(r.pos);
        }
    } 
}

/*
    Function: initialize_world_state
    Objective: Initialize the world state based on the world and robots knowledge. We also use the semantic mappings

    @ Input 1: The robots database object
    @ Input 2: The world knowledge database object
    @ Input 3: A reference to the initial world state vector
    @ Input 4: A reference to the initial function values (in the future we may remove this)
    @ Input 5: The vector of semantic mappings given in the configuration file
    @ Input 6: The mapping between HDDL types and OCL types
    @ Input 7: The sorts map with the existing objects
    @ Output: Void. The reference to the initial world state vector is initialized
*/ 
void initialize_world_state(KnowledgeBase robotsdb, KnowledgeBase worlddb, vector<ground_literal>& init, vector<pair<ground_literal,int>>& init_functions, 
                                vector<SemanticMapping> semantic_mapping, map<string,string> type_mapping, map<string,set<string>> sorts) {
    /*
        Here we will create the following native predicates:
            - at ?r - robot ?rloc - robotlocation
            - hascapability ?r - robot ?c - capability
        
        -> When we have the configuration file we can initialize user-defined predicates and probabilistic predicates
    */
    pt::ptree worlddb_root;
    if(worlddb.get_root_key() == "") {
        worlddb_root = worlddb.get_knowledge();
    } else {
        worlddb_root = worlddb.get_knowledge().get_child(worlddb.get_root_key());
    }

    pt::ptree robotsdb_root;
    if(robotsdb.get_root_key() == "") {
        robotsdb_root = robotsdb.get_knowledge();
    } else {
        robotsdb_root = robotsdb.get_knowledge().get_child(robotsdb.get_root_key());
    }

    //Initialize at predicates
    for(Robot r : assigned_robots) {
        ground_literal l;
        l.predicate = "at";
        l.positive = true;
        l.args.push_back(r.name);
        l.args.push_back(r.pos);

        init.push_back(l);
    }

    for(Robot r : assigned_robots) {
        BOOST_FOREACH(pt::ptree::value_type& child, robotsdb_root) {
            if(child.first == "robot") {
                string robot_name = child.second.get<string>("name");
                if(r.name == robot_name){
                    pt::ptree robot_attr = child.second.get_child("capabilities").get_child("<xmlattr>");
                    int cap_number = robot_attr.get<int>("number");

                    stringstream ss(child.second.get_child("capabilities").data());
                    for(int i = 0;i < cap_number;i++) {
                        ground_literal l;
                            
                        l.predicate = "hascapability";
                        l.positive = true;
                        l.args.push_back(r.name);

                        string cap_name;
                        ss >> cap_name;

                        l.args.push_back(cap_name);
                            
                        init.push_back(l);
                    }
                }
            }
        }
    }

    /*
        Initialize predicates from semantic mappings given at the configuration file

        -> Note that when mapping attributes we are only mapping boolean values
    */
    for(SemanticMapping sm : semantic_mapping) {
        //For now we are only mapping attributes
        if(sm.get_mapping_type() == "attribute") {
            string attr_name = get<string>(sm.get_prop("name"));
            string relation_type = get<string>(sm.get_prop("relation"));
            string db = get<string>(sm.get_prop("belongs_to"));

            //In addition to only mapping attributes we are only mapping them to predicates
            if(sm.get_mapped_type() == "predicate") {
                predicate_definition pred = get<predicate_definition>(sm.get_prop("map"));
                    
                pt::ptree used_db;
                 if(db == "robots_db") {
                    used_db = robotsdb_root;
                } else {
                    used_db = worlddb_root;
                }
                            
                BOOST_FOREACH(pt::ptree::value_type& child, used_db) {
                    if(child.first == relation_type) {
                        //Only insert the predicate if the object exists (was initialized) in the sorts map
                        string hddl_type = type_mapping[relation_type];
                        if(sorts[hddl_type].find(child.second.get<string>("name")) != sorts[hddl_type].end()) {
                            bool val;
                            istringstream(boost::to_lower_copy(child.second.get<string>(attr_name))) >> std::boolalpha >> val;

                            ground_literal l;

                            l.predicate = pred.name;
                            l.positive = val;
                            /*
                                For now, semantic mappings only involve one argument, which is of the hddl_type. With this in mind,
                                we get the name attribute in the xml
                            */
                            for(string sort_type : pred.argument_sorts) {
                                if(sort_type == hddl_type) {
                                    l.args.push_back(child.second.get<string>("name"));
                                }
                            }

                            init.push_back(l);
                        }
                    }
                }
            }
        }
    }

    /*
        Now, it's time for function initialization. For now, we will have manual initialization of these since for automatic
        initialization we will need the configuration file

        -> IMPORTANT: THIS IS A MANUAL INITIALIZATION THAT ONLY WORKS FOR OUR ROOM PREPARATION CASE STUDY!
    */
    ground_literal l;

    l.predicate = "cleaning-time";
    l.positive = true;
    l.args.push_back("r1");
    l.args.push_back("RoomA");

    init_functions.push_back(make_pair(l,30));

    ground_literal l1;

    l1.predicate = "cleaning-time";
    l1.positive = true;
    l1.args.push_back("r1");
    l1.args.push_back("RoomB");

    init_functions.push_back(make_pair(l1,20));

    ground_literal l2;

    l2.predicate = "cleaning-time";
    l2.positive = true;
    l2.args.push_back("r2");
    l2.args.push_back("RoomA");

    init_functions.push_back(make_pair(l2,40));

    ground_literal l3;

    l3.predicate = "cleaning-time";
    l3.positive = true;
    l3.args.push_back("r2");
    l3.args.push_back("RoomB");

    init_functions.push_back(make_pair(l3,15));

    ground_literal l4;

    l4.predicate = "moveobject-time";
    l4.positive = true;
    l4.args.push_back("r3");
    l4.args.push_back("r4");
    l4.args.push_back("RoomA");

    init_functions.push_back(make_pair(l4,50));

    ground_literal l5;

    l5.predicate = "moveobject-time";
    l5.positive = true;
    l5.args.push_back("r3");
    l5.args.push_back("r4");
    l5.args.push_back("RoomB");

    init_functions.push_back(make_pair(l5,35));

    /*
        Now, it's time for reliability initialization
        
        -> These predicates will populate init_rel, where we have to each robot and each task one value for the reliability of that task
        -> For this to be done, we need to add information to the Robots_db.xml, in order to express probability of success for each robot for each task
            - Check Gricel's XML file
        -> For now, we don't deal with reliabilities
    */
}

/*
    Function: initialize_world_state
    Objective: Initialize the world state based on the world and robots knowledge. We also use the semantic mappings

    @ Input: The world state vector 
    @ Output: Void. The world state is printed in the terminal
*/ 
void print_world_state(vector<ground_literal> world_state) {
    cout << "World state: " << endl;
	for(ground_literal l : world_state) {
		string state;
		if(!l.positive) {
			state += "not ";
		}
		state += l.predicate + " ";

		unsigned int arg_num = 0;
		for(string arg : l.args) {
			state += arg;

			arg_num++;
			if(arg_num != l.args.size()) {
				state += " ";
			}
		}

		cout << state << endl;
	}

	cout << endl;
}