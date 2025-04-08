#pragma once
#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <queue>
#include <algorithm>
#include <functional>  // for std::function
#include <tuple>

using namespace std;

class Graph
{
public:

    // helper functions
    bool isPrime(uint64_t num);
    void parseGraphFile(const string graph_file);

    // helper function to trim whitespace from both ends of a string.
    static inline string trim(const string& s);

    // helper fuction to view graph information
    void printGraphInfo() const;

    // problem set 4 operations
	string findShortestPath(const string& start_node, const string& end_node);
	string findPrimePath(const string& start_node, const string& end_node);

	// Helper methods for bandwidth management and error reporting
	size_t getNodesCount() const { return nodes.size(); }
	size_t getEdgesCount() const { return edges.size(); }

	// constructor
	Graph() = default;
	// destructor
	~Graph() = default;



private:
    vector<string> nodes;
    unordered_set<string> nodes_set;
    unordered_set<int> node_weights;
    vector<pair<string, string>> edges;
    unordered_map<string, unordered_set<string>> adjacency_list;
    unordered_map<string, unordered_map<string, uint64_t>> edge_weights;
};

