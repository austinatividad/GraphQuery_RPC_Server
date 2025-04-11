#include "main.h"
#include "Graph.h"
#include <chrono>
#include <algorithm>
#include <string>
#include <regex>
#include <random>
#include <csignal>


// helper function to sanitize input and prevent injection attacks
std::string sanitizeInput(const std::string& input) {
    // remove any potentially dangerous characters
    std::regex dangerous_chars("[\\<>\\&\\'\\\"\\/]");
    return std::regex_replace(input, dangerous_chars, "");
}

// helper function to compress response data (simple implementation)
bool shouldCompressResponse(size_t contentSize, const std::string& contentType) {
    return contentSize > 1024 && 
           (contentType.find("text/") != std::string::npos || 
            contentType.find("application/json") != std::string::npos);
}

// circuit breaker class to prevent cascading errors
class CircuitBreaker {
private:
    std::atomic<int> failureCount{0};
    std::atomic<bool> circuitOpen{false};
    std::chrono::time_point<std::chrono::steady_clock> resetTime;
    const int threshold = 5; 
    const std::chrono::seconds resetTimeout{30}; 

public:
    bool isOpen() const {
        if (circuitOpen) {
            auto now = std::chrono::steady_clock::now();
            if (now > resetTime) {
                return false;
            }
            return true;
        }
        return false;
    }

    void recordSuccess() {
        failureCount = 0;
        circuitOpen = false;
    }

    void recordFailure() {
        if (++failureCount >= threshold) {
            circuitOpen = true;
            resetTime = std::chrono::steady_clock::now() + resetTimeout;
        }
    }
};

int main() {
	httplib::Server svr;
	Graph graph;
	
	// circuit breakers for different operations
	CircuitBreaker shortestPathCircuit;
	CircuitBreaker primePathCircuit;

	svr.set_read_timeout(10, 0);  
	svr.set_write_timeout(10, 0); 
	
	const size_t MAX_PAYLOAD_SIZE = 5 * 1024 * 1024;
	const size_t MAX_GRAPH_SIZE = 10 * 1024 * 1024; 


	std::vector<std::string> supportedContentTypes = {"text/plain", "application/json"};
	

	const std::string SERVER_ID = "GraphQueryServer/1.0";
	

	std::unordered_map<std::string, std::pair<int, std::chrono::time_point<std::chrono::steady_clock>>> requestCounts;
	const int MAX_REQUESTS_PER_MINUTE = 10;
	const int MAX_PAYLOAD_REQUESTS_PER_MINUTE = 10; 
	
	svr.set_exception_handler([&](const httplib::Request& req, httplib::Response& res, std::exception_ptr ep) {
		
		
		try {
			std::rethrow_exception(ep);
		} catch (const std::ios_base::failure& e) {
			std::cerr << "Network I/O error during request processing: " << e.what() << std::endl;
			res.status = 503; // Service Unavailable
			res.set_header("Retry-After", "30"); // Suggest retry after 30 seconds
			res.set_content("Service temporarily unavailable due to network issues", "text/plain");
		} catch (const std::system_error& e) {
			std::cerr << "System error during request processing: " << e.what() << 
			          " (code: " << e.code() << ")" << std::endl;
			res.status = 500;
			res.set_content("System error: " + std::string(e.what()), "text/plain");
		} catch (const std::runtime_error& e) {
			std::cerr << "Runtime error during request processing: " << e.what() << std::endl;
			res.status = 500;
			res.set_content("Server error: " + std::string(e.what()), "text/plain");
		} catch (const std::exception& e) {
			std::cerr << "Exception during request processing: " << e.what() << std::endl;
			res.status = 500;
			std::string sanitized_error = sanitizeInput(e.what());
			res.set_content("Server error: " + sanitized_error, "text/plain");
		} catch (...) {
			std::cerr << "Unknown exception during request processing" << std::endl;
			res.status = 500;
			res.set_content("Internal server error", "text/plain");
		}
		
		res.set_header("Server", SERVER_ID);
		
		res.set_header("Cache-Control", "no-store, must-revalidate");
		res.set_header("Pragma", "no-cache");
	});
	
	svr.set_error_handler([&](const httplib::Request& req, httplib::Response& res) {
		std::cerr << "HTTP error occurred: " << res.status << 
		          " for request " << req.method << " " << req.path << 
		          " from " << req.remote_addr << std::endl;
		
		std::string message = "Unknown error";
		std::string additional_info = "";
		
		switch (res.status) {
			case 400: 
				message = "Bad Request"; 
				additional_info = "The request could not be understood due to malformed syntax."; 
				break;
			case 404: 
				message = "Not Found"; 
				additional_info = "The requested resource could not be found. Available endpoints: /heartbeat, /graph_info, /initialize, /shortest_path, /prime_path"; 
				break;
			case 408: 
				message = "Request Timeout"; 
				additional_info = "The server timed out waiting for the request. Please try again with a simpler query or smaller payload."; 
				break;
			case 413: 
				message = "Payload Too Large"; 
				additional_info = "The request payload exceeds the server's limits. Max size for graph initialization: " + 
				               std::to_string(MAX_GRAPH_SIZE/1024/1024) + "MB, other requests: " + 
				               std::to_string(MAX_PAYLOAD_SIZE/1024/1024) + "MB."; 
				break;
			case 429: 
				message = "Too Many Requests"; 
				additional_info = "You have sent too many requests in a given amount of time. Please wait before trying again."; 
				res.set_header("Retry-After", "60"); 
				break;
			case 500: 
				message = "Internal Server Error"; 
				additional_info = "The server encountered an unexpected condition. Please report this issue."; 
				break;
			case 503: 
				message = "Service Unavailable"; 
				additional_info = "The server is currently unable to handle the request due to temporary overloading or maintenance."; 
				res.set_header("Retry-After", "120");
				break;
			case 507: 
				message = "Insufficient Storage"; 
				additional_info = "The server has insufficient storage to complete the request. Try with a smaller graph."; 
				break;
		}
		
		std::string response_body = message;
		if (!additional_info.empty()) {
			response_body += ": " + additional_info;
		}

		std::random_device rd;
		std::mt19937 gen(rd());
		std::uniform_int_distribution<> dis(10000000, 99999999);
		std::string request_id = std::to_string(dis(gen));
		
		res.set_header("X-Request-ID", request_id);
		res.set_header("Server", SERVER_ID);
		res.set_content(response_body, "text/plain");

		res.set_header("Cache-Control", "no-store, must-revalidate");
		res.set_header("Pragma", "no-cache");
	});

	auto server_start_time = std::chrono::steady_clock::now();
	
	svr.Get("/heartbeat", [&](const httplib::Request& req, httplib::Response& res) {
		std::cout << "Received request: " << req.path << " from " << req.remote_addr << endl;
		

		auto now = std::chrono::steady_clock::now();
		auto& client_info = requestCounts[req.remote_addr];

		if (now - client_info.second > std::chrono::minutes(1)) {
			client_info.first = 0;
			client_info.second = now;
		}

		client_info.first++;

		if (client_info.first > MAX_REQUESTS_PER_MINUTE) {
			res.status = 429;
			res.set_header("Retry-After", "60");
			res.set_content("Rate limit exceeded. Please try again later.", "text/plain");
			return;
		}
		
		try {
			auto uptime_duration = std::chrono::steady_clock::now() - server_start_time;
			auto uptime_hours = std::chrono::duration_cast<std::chrono::hours>(uptime_duration).count();
			auto uptime_minutes = std::chrono::duration_cast<std::chrono::minutes>(uptime_duration).count() % 60;
			auto uptime_seconds = std::chrono::duration_cast<std::chrono::seconds>(uptime_duration).count() % 60;

			std::stringstream status;
			status << "Status: Running\n"
			       << "Server ID: " << SERVER_ID << "\n"
			       << "Uptime: " << uptime_hours << "h " << uptime_minutes << "m " << uptime_seconds << "s\n"
			       << "Endpoints: /heartbeat, /graph_info, /initialize, /shortest_path, /prime_path\n"
			       << "Content types: " << supportedContentTypes[0];
			
			for (size_t i = 1; i < supportedContentTypes.size(); i++) {
				status << ", " << supportedContentTypes[i];
			}

			status << "\nShortestPath service: " << (shortestPathCircuit.isOpen() ? "degraded" : "available");
			status << "\nPrimePath service: " << (primePathCircuit.isOpen() ? "degraded" : "available");

			res.set_header("Server", SERVER_ID);
			res.set_header("X-Content-Type-Options", "nosniff");
			res.set_header("Cache-Control", "no-cache, max-age=0");

			std::string content_type = "text/plain";
			if (req.has_header("Accept") && req.get_header_value("Accept").find("application/json") != std::string::npos) {
				content_type = "application/json";
				std::string json = "{\"status\":\"running\",\"server_id\":\"" + SERVER_ID + "\","
				                 "\"uptime_seconds\":" + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(uptime_duration).count()) + ","
				                 "\"shortest_path_available\":" + std::string(shortestPathCircuit.isOpen() ? "false" : "true") + ","
				                 "\"prime_path_available\":" + std::string(primePathCircuit.isOpen() ? "false" : "true") + "}";
				res.set_content(json, content_type);
			} else {
				res.set_content(status.str(), content_type);
			}
		} catch (const std::exception& e) {
			std::cerr << "Error in heartbeat handler: " << e.what() << std::endl;
			res.status = 500;
			res.set_content("Server error: " + sanitizeInput(e.what()), "text/plain");
		} catch (...) {
			std::cerr << "Unknown error in heartbeat handler" << std::endl;
			res.status = 500;
			res.set_content("Server error: Unknown error", "text/plain");
		}
	});

	
	// Utility Routes

	// ------ Route to print graph information (nodes, edges, weights)
		// this is a get request with query parameters of (node1, node2)
		// this will print the graph information to the console
		// this will return a 200 OK response with the graph information in the body

	svr.Get("/graph_info", [&](const httplib::Request& req, httplib::Response& res) {
		cout << "Received request: " << req.path << " from " << req.remote_addr << endl;

		auto now = std::chrono::steady_clock::now();
		auto& client_info = requestCounts[req.remote_addr];
		

		if (now - client_info.second > std::chrono::minutes(1)) {
			client_info.first = 0;
			client_info.second = now;
		}

		client_info.first++;

		if (client_info.first > MAX_REQUESTS_PER_MINUTE) {
			res.status = 429; 
			res.set_header("Retry-After", "60");
			res.set_content("Rate limit exceeded. Please try again later.", "text/plain");
			return;
		}

		try {
			auto start_time = std::chrono::high_resolution_clock::now();
			
			graph.printGraphInfo();

			auto end_time = std::chrono::high_resolution_clock::now();
			auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
			cout << "Graph info printed in " << duration << "ms" << endl;

			res.set_header("Server", SERVER_ID);
			res.set_header("X-Processing-Time", std::to_string(duration) + "ms");

			std::string content_type = "text/plain";
			std::string response_content;
			
			if (req.has_header("Accept") && req.get_header_value("Accept").find("application/json") != std::string::npos) {
				content_type = "application/json";
				response_content = "{\"message\":\"Graph information printed to server console.\",\"duration_ms\":" + 
				                   std::to_string(duration) + "}";
			} else {
				response_content = "Graph information printed to server console. (Processed in " + 
				                   std::to_string(duration) + "ms)";
			}
			
			res.set_header("Cache-Control", "no-cache, max-age=0");

			if (shouldCompressResponse(response_content.size(), content_type)) {
				res.set_header("X-Compression-Applied", "would-be-gzip");
			}
			
			res.set_content(response_content, content_type);
		} catch (const std::exception& e) {
			std::cerr << "Error printing graph info: " << e.what() << std::endl;
			res.status = 500;
			res.set_content("Failed to print graph info: " + sanitizeInput(e.what()), "text/plain");
		} catch (...) {
			std::cerr << "Unknown error printing graph info" << std::endl;
			res.status = 500;
			res.set_content("Failed to print graph info: Unknown error", "text/plain");
		}
	});





	// ------- Route for initializing graph structure -------
		// this is a post request that contains the graph.txt as data
	svr.Post("/initialize", [&](const httplib::Request& req, httplib::Response& res) {
		cout << "Received /initialize request with body size: " << req.body.size() << " bytes from " << req.remote_addr << endl;
		
		auto now = std::chrono::steady_clock::now();
		auto& client_info = requestCounts[req.remote_addr];

		if (now - client_info.second > std::chrono::minutes(1)) {
			client_info.first = 0;
			client_info.second = now;
		}

		client_info.first++;

		if (client_info.first > MAX_PAYLOAD_REQUESTS_PER_MINUTE) {
			res.status = 429; // Too Many Requests
			res.set_header("Retry-After", "60");
			res.set_header("X-RateLimit-Limit", std::to_string(MAX_PAYLOAD_REQUESTS_PER_MINUTE));
			res.set_header("X-RateLimit-Remaining", "0");
			res.set_header("X-RateLimit-Reset", std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::minutes(1) - (now - client_info.second)).count()));
			res.set_content("Rate limit exceeded for large payload operations. Please try again later.", "text/plain");
			return;
		}

		if (req.body.size() > MAX_GRAPH_SIZE) {
			res.status = 413; 
			res.set_header("X-Max-Payload-Size", std::to_string(MAX_GRAPH_SIZE));
			res.set_content("Request body too large. Maximum allowed size is " + 
			               std::to_string(MAX_GRAPH_SIZE/1024/1024) + "MB", "text/plain");
			return;
		}
		
		if (req.body.empty()) {
			res.status = 400;
			res.set_content("Empty graph data provided. Please provide valid graph data.", "text/plain");
			return;
		}
		
		std::string content_type = req.has_header("Content-Type") ? req.get_header_value("Content-Type") : "";
		if (!content_type.empty() && 
			content_type.find("text/plain") == std::string::npos && 
			content_type.find("application/octet-stream") == std::string::npos) {
			res.status = 415; 
			res.set_content("Unsupported content type. Please provide graph data as text/plain or application/octet-stream.", "text/plain");
			return;
		}
		bool valid_format = false;
		size_t node_count = 0;
		size_t edge_count = 0;
		std::istringstream iss(req.body);
		std::string line;

		while (std::getline(iss, line)) {
			line = Graph::trim(line);
			if (line.empty()) continue;
			
			if (line[0] == '*') {
				valid_format = true;
				node_count++;
			} else if (line[0] == '-') {
				valid_format = true;
				edge_count++;
			}
		}
		
		if (!valid_format) {
			res.status = 400;
			res.set_content("Invalid graph format. Graph data must contain node definitions (lines starting with '*') " 
						  "and/or edge definitions (lines starting with '-').", "text/plain");
			return;
		}
		
		std::string sanitized_body = sanitizeInput(req.body);
		
		try {
			auto start_time = std::chrono::high_resolution_clock::now();

			graph.parseGraphFile(sanitized_body);
			
			auto end_time = std::chrono::high_resolution_clock::now();
			auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
			cout << "Graph initialization completed in " << duration << "ms" << endl;

			size_t actual_nodes = graph.getNodesCount();
			size_t actual_edges = graph.getEdgesCount();
			
			res.set_header("Server", SERVER_ID);
			res.set_header("X-Processing-Time", std::to_string(duration) + "ms");
			res.set_header("X-Graph-Nodes", std::to_string(actual_nodes));
			res.set_header("X-Graph-Edges", std::to_string(actual_edges));
			
			std::string response_content_type = "text/plain";
			std::string response_content;
			
			if (req.has_header("Accept") && req.get_header_value("Accept").find("application/json") != std::string::npos) {
				response_content_type = "application/json";
				response_content = "{\"status\":\"success\",\"message\":\"Graph initialized successfully\","
								 "\"nodes\":" + std::to_string(actual_nodes) + ","
								 "\"edges\":" + std::to_string(actual_edges) + ","
								 "\"duration_ms\":" + std::to_string(duration) + "}";
			} else {
				response_content = "Graph initialized successfully:\n" +
								std::string("- Nodes: ") + std::to_string(actual_nodes) + "\n" +
								 "- Edges: " + std::to_string(actual_edges) + "\n" +
								 "- Processing time: " + std::to_string(duration) + "ms";
			}

			res.set_header("Cache-Control", "no-cache, max-age=0");
			

			if (shouldCompressResponse(response_content.size(), response_content_type)) {
				res.set_header("X-Compression-Applied", "would-be-gzip");
				res.set_header("Content-Encoding", "gzip");
			}
			
			res.set_content(response_content, response_content_type);
		} catch (const std::exception& e) {
			std::cerr << "Error during graph initialization: " << e.what() << std::endl;
			res.status = 500;
			res.set_content("Failed to initialize graph: " + sanitizeInput(e.what()), "text/plain");
		} catch (...) {
			std::cerr << "Unknown error during graph initialization" << std::endl;
			res.status = 500;
			res.set_content("Failed to initialize graph: Unknown error", "text/plain");
		}
	});

	// ------- Route for shortest-path command -------
		// this is a get request with query parameters of (node1, node2)
	svr.Post("/shortest_path", [&](const httplib::Request& req, httplib::Response& res) {
		cout << "Received /shortest_path request with body size: " << req.body.size() << " bytes from " << req.remote_addr << endl;

		if (shortestPathCircuit.isOpen()) {
			res.status = 503;
			res.set_header("Retry-After", "30");
			res.set_content("Shortest path service is temporarily unavailable. Please try again later.", "text/plain");
			return;
		}


		auto now = std::chrono::steady_clock::now();
		auto& client_info = requestCounts[req.remote_addr];
		

		if (now - client_info.second > std::chrono::minutes(1)) {
			client_info.first = 0;
			client_info.second = now;
		}
		
		client_info.first++;

		if (client_info.first > MAX_REQUESTS_PER_MINUTE) {
			res.status = 429; 
			res.set_header("Retry-After", "60");
			res.set_content("Rate limit exceeded. Please try again later.", "text/plain");
			return;
		}

		if (req.body.size() > MAX_PAYLOAD_SIZE) {
			res.status = 413; 
			res.set_content("Request body too large", "text/plain");
			return;
		}

		istringstream iss(req.body);
		string start_node, end_node;
		iss >> start_node >> end_node;

		if (start_node.empty() || end_node.empty()) {
			res.status = 400;
			res.set_content("Invalid input: start_node and end_node required", "text/plain");
			return;
		}

		start_node = sanitizeInput(start_node);
		end_node = sanitizeInput(end_node);

		try {
			auto start_time = std::chrono::high_resolution_clock::now();
			
			string result = graph.findShortestPathParallel(start_node, end_node);

			auto end_time = std::chrono::high_resolution_clock::now();
			auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
			cout << "Shortest path calculation completed in " << duration << "ms" << endl;
			
			if (duration > 9000) { 
				cout << "Warning: Shortest path calculation approaching timeout threshold" << endl;
			}
			
			shortestPathCircuit.recordSuccess();
			

			res.set_header("Server", SERVER_ID);
			res.set_header("X-Processing-Time", std::to_string(duration) + "ms");
			

			std::string content_type = "text/plain";
			std::string response_content;
			
			if (req.has_header("Accept") && req.get_header_value("Accept").find("application/json") != std::string::npos) {
				content_type = "application/json";
				response_content = "{\"path\":\"" + result + "\",\"duration_ms\":" + 
				                   std::to_string(duration) + "}";
			} else {
				response_content = result;
			}
			

			if (shouldCompressResponse(response_content.size(), content_type)) {
				res.set_header("X-Compression-Applied", "would-be-gzip");
			}
			
			res.set_content(response_content, content_type);
		} catch (const std::exception& e) {

			shortestPathCircuit.recordFailure();
			

			std::cerr << "Error during shortest path calculation: " << e.what() << std::endl;
			res.status = 500;
			res.set_content("Failed to calculate shortest path: " + sanitizeInput(e.what()), "text/plain");
		} catch (...) {

			shortestPathCircuit.recordFailure();
			
			std::cerr << "Unknown error during shortest path calculation" << std::endl;
			res.status = 500;
			res.set_content("Failed to calculate shortest path: Unknown error", "text/plain");
		}
	});

	// ------- Route for prime-path command -------
		// this is a get request with query parameters of (node1, node2)
	svr.Post("/prime_path", [&](const httplib::Request& req, httplib::Response& res) {
		cout << "Received /prime_path request with body size: " << req.body.size() << " bytes from " << req.remote_addr << endl;

		if (primePathCircuit.isOpen()) {
			res.status = 503;
			res.set_header("Retry-After", "30");
			res.set_content("Prime path service is temporarily unavailable. Please try again later.", "text/plain");
			return;
		}


		auto now = std::chrono::steady_clock::now();
		auto& client_info = requestCounts[req.remote_addr];
		
		if (now - client_info.second > std::chrono::minutes(1)) {
			client_info.first = 0;
			client_info.second = now;
		}
		

		client_info.first++;
		
		if (client_info.first > MAX_REQUESTS_PER_MINUTE) {
			res.status = 429; 
			res.set_header("Retry-After", "60");
			res.set_content("Rate limit exceeded. Please try again later.", "text/plain");
			return;
		}

		if (req.body.size() > MAX_PAYLOAD_SIZE) {
			res.status = 413;
			res.set_content("Request body too large", "text/plain");
			return;
		}

		istringstream iss(req.body);
		string start_node, end_node;
		iss >> start_node >> end_node;

		if (start_node.empty() || end_node.empty()) {
			res.status = 400; 
			res.set_content("Invalid input: start_node and end_node required", "text/plain");
			return;
		}

		start_node = sanitizeInput(start_node);
		end_node = sanitizeInput(end_node);

		try {
			auto start_time = std::chrono::high_resolution_clock::now();
			
			string result = graph.findPrimePathParallel(start_node, end_node);

			auto end_time = std::chrono::high_resolution_clock::now();
			auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
			cout << "Prime path calculation completed in " << duration << "ms" << endl;
			
			if (duration > 9000) {
				cout << "Warning: Prime path calculation approaching timeout threshold" << endl;
			}

			primePathCircuit.recordSuccess();
			

			res.set_header("Server", SERVER_ID);
			res.set_header("X-Processing-Time", std::to_string(duration) + "ms");
			
			std::string content_type = "text/plain";
			std::string response_content;
			
			if (req.has_header("Accept") && req.get_header_value("Accept").find("application/json") != std::string::npos) {
				content_type = "application/json";
				response_content = "{\"path\":\"" + result + "\",\"duration_ms\":" + 
				                   std::to_string(duration) + "}";
			} else {
				response_content = result;
			}
			
			if (shouldCompressResponse(response_content.size(), content_type)) {
				res.set_header("X-Compression-Applied", "would-be-gzip");
			}
			
			res.set_content(response_content, content_type);
		} catch (const std::exception& e) {

			primePathCircuit.recordFailure();
			

			std::cerr << "Error during prime path calculation: " << e.what() << std::endl;
			res.status = 500;
			res.set_content("Failed to calculate prime path: " + sanitizeInput(e.what()), "text/plain");
		} catch (...) {

			primePathCircuit.recordFailure();
			
			std::cerr << "Unknown error during prime path calculation" << std::endl;
			res.status = 500;
			res.set_content("Failed to calculate prime path: Unknown error", "text/plain");
		}
	});


	svr.set_keep_alive_max_count(100);
	
	
	cout << "Server started at http://localhost:8080" << endl;
	cout << "Press Ctrl+C to stop the server" << endl;

	std::cout << "Starting " << SERVER_ID << " on http://localhost:8080" << std::endl;
	std::cout << "Available endpoints:" << std::endl;
	std::cout << "  - GET  /heartbeat" << std::endl;
	std::cout << "  - GET  /graph_info" << std::endl;
	std::cout << "  - POST /initialize" << std::endl;
	std::cout << "  - POST /shortest_path" << std::endl;
	std::cout << "  - POST /prime_path" << std::endl;
	std::cout << "  - GET  /service_discovery" << std::endl;

	if (!svr.listen("0.0.0.0", 8080)) {
		std::cerr << "Failed to start server!" << std::endl;
		return 1;
	}
	
	return 0;
}