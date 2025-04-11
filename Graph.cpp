#include "Graph.h"

bool Graph::isPrime(uint64_t num)
{
    if (num <= 1 || (num % 2 == 0 && num > 2)) return false;
    // Check odd divisors from 3 up to square root of num
    // We only need to check odd numbers since we already handled even numbers above
    // We only need to check up to sqrt(num) since factors come in pairs
    for (uint64_t i = 3; i <= sqrt(num); i += 2) {
        if (num % i == 0)
            return false;
    }
    return true;
}

void Graph::parseGraphFile(const string graph_file) {
    // Clear any existing data
    nodes.clear();
    nodes_set.clear();
    edges.clear();
    adjacency_list.clear();
    edge_weights.clear();

    istringstream iss(graph_file);
    string line;

    while (getline(iss, line)) {
        // Trim the line
        line = trim(line);
        if (line.empty())
            continue;

        // check the first character to determine node or edge.
        if (line[0] == '*') {
            // node definition: "* a" means node "a"
            // get the substring after '*' and trim any whitespace.
            string node_label = trim(line.substr(1));
            if (!node_label.empty()) {
                // Add node if it doesn't already exist
                if (nodes_set.find(node_label) == nodes_set.end()) {
                    nodes.push_back(node_label);
                    nodes_set.insert(node_label);
                }
            }
        }
        else if (line[0] == '-') {
            // edge definition: "- a b 3" means an edge from "a" to "b" with weight 3.
            // remove the '-' and then parse tokens.
            string tokenStr = trim(line.substr(1));
            istringstream tokenStream(tokenStr);
            string source, target;
            uint64_t weight;
            tokenStream >> source >> target >> weight;

            if (!source.empty() && !target.empty()) {
                // Ensure both nodes exist in our data structures.
                if (nodes_set.find(source) == nodes_set.end()) {
                    nodes.push_back(source);
                    nodes_set.insert(source);
                }
                if (nodes_set.find(target) == nodes_set.end()) {
                    nodes.push_back(target);
                    nodes_set.insert(target);
                }

                // store the edge
                edges.push_back({ source, target });
                adjacency_list[source].insert(target);
                edge_weights[source][target] = weight;
            }
        }
    }
}

inline string Graph::trim(const string& s)
{
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

void Graph::printGraphInfo() const
{
	cout << "Nodes: ";
	for (const auto& node : nodes) {
		cout << node << " ";
	}
	cout << endl;

	cout << "Edges: ";
	for (const auto& edge : edges) {
		cout << "(" << edge.first <<  "--> " << edge.second << ") ";
	}
	cout << endl;

    cout << "Edge Weights: ";
	for (const auto& edge : edges) {
		cout << "(" << edge.first << " --> " << edge.second << ": " << edge_weights.at(edge.first).at(edge.second) << ") ";
	}
	cout << endl;
}


string Graph::findShortestPath(const string& start_node, const string& end_node)
{
    // Check if both nodes exist in the graph.
    if (nodes_set.find(start_node) == nodes_set.end() || nodes_set.find(end_node) == nodes_set.end()) {
        return "No path from " + start_node + " to " + end_node;
    }

    // We'll store each found path along with its total weight.
    // The pair consists of: (the path as a vector of node labels, total weight)
    vector<pair<vector<string>, uint64_t>> all_paths;

    // Define a lambda for DFS that collects paths.
    // current: current node
    // path: the current path taken so far
    // visited: the set of nodes visited on the current path
    // current_weight: the total weight accumulated so far
    std::function<void(const string&, vector<string>&, unordered_set<string>&, uint64_t)> dfs;
    dfs = [&](const string& current, vector<string>& path, unordered_set<string>& visited, uint64_t current_weight) {
        if (current == end_node) {
            // Found a complete path; store a copy of it along with its total weight.
            all_paths.push_back({ path, current_weight });
            return;
        }

        // If no neighbors exist, return.
        if (adjacency_list.find(current) == adjacency_list.end()) {
            return;
        }

        // Recurse for every neighbor that hasn't been visited.
        for (const string& neighbor : adjacency_list[current]) {
            if (visited.find(neighbor) == visited.end()) {
                visited.insert(neighbor);
                path.push_back(neighbor);
                uint64_t w = edge_weights[current][neighbor];  // edge weight from current to neighbor
                dfs(neighbor, path, visited, current_weight + w);
                path.pop_back();
                visited.erase(neighbor);
            }
        }
        };

    // Initialize DFS with the start node.
    vector<string> path = { start_node };
    unordered_set<string> visited = { start_node };
    dfs(start_node, path, visited, 0);

    // If no path was found, return a message.
    if (all_paths.empty()) {
        return "No path from " + start_node + " to " + end_node;
    }

    // Find the path with the minimum total weight.
    auto best = all_paths[0];
    for (const auto& p : all_paths) {
        if (p.second < best.second) {
            best = p;
        }
    }

    // Build the result string in the desired format:
    // For each pair in the path, include the edge weight between them.
    string result;
    for (size_t i = 0; i < best.first.size() - 1; ++i) {
        string current = best.first[i];
        string next = best.first[i + 1];
        uint64_t w = edge_weights[current][next];
        result += current + " -{" + to_string(w) + "}-> ";
    }
    result += best.first.back();
    result += " = " + to_string(best.second);

    return result;
}

string Graph::findPrimePath(const string& start_node, const string& end_node) {
    // Check if both start and end nodes exist in the graph.
    if (nodes_set.find(start_node) == nodes_set.end() || nodes_set.find(end_node) == nodes_set.end())
        return "No prime path from " + start_node + " to " + end_node;

    // Define a queue where each element is a tuple:
    // (current node, path taken so far, total weight so far)
    queue<tuple<string, vector<string>, uint64_t>> q;
    q.push(make_tuple(start_node, vector<string>{start_node}, 0));

    while (!q.empty()) {
        auto frontItem = q.front();
        string current = get<0>(frontItem);
        vector<string> path = get<1>(frontItem);
        uint64_t total_weight = get<2>(frontItem);


        q.pop();

        if (current == end_node) {
            if (isPrime(total_weight)) {
                // Format the output: "a -{weight}-> b -{weight}-> c = {total_weight} is prime!"
                string result;
                for (size_t i = 0; i < path.size() - 1; ++i) {
                    string src = path[i];
                    string dst = path[i + 1];
                    uint64_t edge_w = 1;
                    if (edge_weights.count(src) && edge_weights[src].count(dst))
                        edge_w = edge_weights[src][dst];
                    result += src + " -{" + to_string(edge_w) + "}-> ";
                }
                result += path.back();
                result += " = " + to_string(total_weight) + " is prime!";
                return result;
            }
            // If the weight is not prime, continue searching for other paths.
        }

        // Expand the search to the neighbors of the current node.
        if (adjacency_list.find(current) != adjacency_list.end()) {
            for (const auto& neighbor : adjacency_list[current]) {
                // To avoid cycles, check that neighbor is not already in the path.
                if (find(path.begin(), path.end(), neighbor) == path.end()) {
                    uint64_t edge_w = 1;
                    if (edge_weights.count(current) && edge_weights[current].count(neighbor))
                        edge_w = edge_weights[current][neighbor];
                    vector<string> new_path = path;
                    new_path.push_back(neighbor);
                    q.push(make_tuple(neighbor, new_path, total_weight + edge_w));
                }
            }
        }
    }

    // If no path with a prime total weight is found, return an appropriate message.
    return "No prime path from " + start_node + " to " + end_node;
}

string Graph::findShortestPathParallel(const string& start_node, const string& end_node)
{
    if (nodes_set.find(start_node) == nodes_set.end() || nodes_set.find(end_node) == nodes_set.end()) {
        return "No path from " + start_node + " to " + end_node;
    }

    vector<pair<vector<string>, uint64_t>> all_paths;
    mutex all_paths_mutex;

    atomic<int> active_tasks{ 0 };
    condition_variable cv;
    mutex cv_mutex;

    function<void(const string&, vector<string>, unordered_set<string>, uint64_t)> dfs_task;
    dfs_task = [&](const string& current, vector<string> path, unordered_set<string> visited, uint64_t weight) {
        if (current == end_node) {
            lock_guard<mutex> lock(all_paths_mutex);
            all_paths.emplace_back(path, weight);
            if (--active_tasks == 0) cv.notify_one();
            return;
        }

        if (adjacency_list.find(current) == adjacency_list.end()) {
            if (--active_tasks == 0) cv.notify_one();
            return;
        }

        for (const auto& neighbor : adjacency_list[current]) {
            if (visited.find(neighbor) == visited.end()) {
                vector<string> new_path = path;
                unordered_set<string> new_visited = visited;

                new_path.push_back(neighbor);
                new_visited.insert(neighbor);

                uint64_t edge_weight = edge_weights[current][neighbor];
                uint64_t new_weight = weight + edge_weight;

                active_tasks++;
                threadPool.enqueue([&, neighbor, new_path, new_visited, new_weight]() {
                    dfs_task(neighbor, new_path, new_visited, new_weight);
                    });
            }
        }

        if (--active_tasks == 0) cv.notify_one();
        };

    vector<string> start_path = { start_node };
    unordered_set<string> start_visited = { start_node };

    active_tasks++;
    dfs_task(start_node, start_path, start_visited, 0);

    // Wait for all tasks to complete
    unique_lock<mutex> lock(cv_mutex);
    cv.wait(lock, [&] { return active_tasks == 0; });

    if (all_paths.empty()) {
        return "No path from " + start_node + " to " + end_node;
    }

    auto best = all_paths[0];
    for (const auto& p : all_paths) {
        if (p.second < best.second) {
            best = p;
        }
    }

    string result;
    for (size_t i = 0; i < best.first.size() - 1; ++i) {
        string current = best.first[i];
        string next = best.first[i + 1];
        uint64_t w = edge_weights[current][next];
        result += current + " -{" + to_string(w) + "}-> ";
    }
    result += best.first.back();
    result += " = " + to_string(best.second);

    return result;
}



string Graph::findPrimePathParallel(const string& start_node, const string& end_node)
{
    if (nodes_set.find(start_node) == nodes_set.end() || nodes_set.find(end_node) == nodes_set.end()) {
        return "No prime path from " + start_node + " to " + end_node;
    }

    vector<pair<vector<string>, uint64_t>> prime_paths;
    mutex prime_paths_mutex;

    atomic<int> active_tasks{ 0 };
    condition_variable cv;
    mutex cv_mutex;

    function<void(const string&, vector<string>, unordered_set<string>, uint64_t)> dfs_task;
    dfs_task = [&](const string& current, vector<string> path, unordered_set<string> visited, uint64_t weight) {
        if (current == end_node && isPrime(weight)) {
            lock_guard<mutex> lock(prime_paths_mutex);
            prime_paths.emplace_back(path, weight);
        }

        if (adjacency_list.find(current) == adjacency_list.end()) {
            if (--active_tasks == 0) cv.notify_one();
            return;
        }

        for (const auto& neighbor : adjacency_list[current]) {
            if (visited.find(neighbor) == visited.end()) {
                vector<string> new_path = path;
                unordered_set<string> new_visited = visited;

                new_path.push_back(neighbor);
                new_visited.insert(neighbor);

                uint64_t edge_weight = edge_weights[current][neighbor];
                uint64_t new_weight = weight + edge_weight;

                active_tasks++;
                threadPool.enqueue([&, neighbor, new_path, new_visited, new_weight]() {
                    dfs_task(neighbor, new_path, new_visited, new_weight);
                    });
            }
        }

        if (--active_tasks == 0) cv.notify_one();
        };

    vector<string> start_path = { start_node };
    unordered_set<string> start_visited = { start_node };

    active_tasks++;
    dfs_task(start_node, start_path, start_visited, 0);

    // Wait for all tasks to complete
    unique_lock<mutex> lock(cv_mutex);
    cv.wait(lock, [&] { return active_tasks == 0; });

    if (prime_paths.empty()) {
        return "No prime path from " + start_node + " to " + end_node;
    }

    // Return the shortest prime path found (or customize this to return the first/favored/etc.)
    auto best = prime_paths[0];
    for (const auto& p : prime_paths) {
        if (p.second < best.second) {
            best = p;
        }
    }

    string result;
    for (size_t i = 0; i < best.first.size() - 1; ++i) {
        string current = best.first[i];
        string next = best.first[i + 1];
        uint64_t w = edge_weights[current][next];
        result += current + " -{" + to_string(w) + "}-> ";
    }
    result += best.first.back();
    result += " = " + to_string(best.second) + " is prime!";

    return result;
}

