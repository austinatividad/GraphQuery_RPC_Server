#include "main.h"
#include "Graph.h"


int main() {
	httplib::Server svr;
	Graph graph;

	// Route to test the server
	svr.Get("/heartbeat", [](const httplib::Request& req, httplib::Response& res) {
		// send a 200 OK response with a message
		cout << "Received request: " << req.path << endl;
		res.set_content("Server is running!", "text/plain");
		});

	
	// Utility Routes

	// ------ Route to print graph information (nodes, edges, weights)
		// this is a get request with query parameters of (node1, node2)
		// this will print the graph information to the console
		// this will return a 200 OK response with the graph information in the body

	svr.Get("/graph_info", [&graph](const httplib::Request& req, httplib::Response& res) {
		cout << "Received request: " << req.path << endl;

		// call graph.printGraphInfo()
		graph.printGraphInfo();


		res.set_content("Graph information printed to server console.", "text/plain");
		});





	// ------- Route for initializing graph structure -------
		// this is a post request that contains the graph.txt as data
	svr.Post("/initialize", [&graph](const httplib::Request& req, httplib::Response& res) {
		cout << "Received /initialize request with body size: " << req.body.size() << " bytes" << endl;
		// Call your Graph class function to parse the graph file data from the request body
		graph.parseGraphFile(req.body);
		// Send a response back to the client confirming initialization
		res.set_content("Graph initialized successfully", "text/plain");
		});

	// ------- Route for shortest-path command -------
		// this is a get request with query parameters of (node1, node2)
	svr.Post("/shortest_path", [&graph](const httplib::Request& req, httplib::Response& res) {
		cout << "Received /shortest_path request with body size: " << req.body.size() << " bytes" << endl;

		// Use istringstream to parse the request body.
		istringstream iss(req.body);
		string start_node, end_node;
		iss >> start_node >> end_node;

		if (start_node.empty() || end_node.empty()) {
			res.status = 400;  // Bad Request
			res.set_content("Invalid input: start_node and end_node required", "text/plain");
			return;
		}

		// Call the Graph method to find the shortest path.
		string result = graph.findShortestPath(start_node, end_node);
		res.set_content(result, "text/plain");
		});

	// ------- Route for prime-path command -------
		// this is a get request with query parameters of (node1, node2)
	svr.Post("/prime_path", [&graph](const httplib::Request& req, httplib::Response& res) {
		cout << "Received /prime_path request with body size: " << req.body.size() << " bytes" << endl;

		// Use istringstream to parse the request body.
		istringstream iss(req.body);
		string start_node, end_node;
		iss >> start_node >> end_node;

		if (start_node.empty() || end_node.empty()) {
			res.status = 400;  // Bad Request
			res.set_content("Invalid input: start_node and end_node required", "text/plain");
			return;
		}

		// Call the Graph method to find the shortest path.
		string result = graph.findPrimePath(start_node, end_node);
		res.set_content(result, "text/plain");
		});


	cout << "Server started at http://localhost:8080" << endl;
	svr.listen("localhost", 8080);
	

	return 0;
}