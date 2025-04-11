#include "main.h"
#include "Graph.h"
#include <chrono>
#include <algorithm>
#include <string>
#include <regex>
#include <random>
#include <csignal>


// Helper function to sanitize input and prevent injection attacks
std::string sanitizeInput(const std::string& input) {
    // Remove any potentially dangerous characters
    std::regex dangerous_chars("[\\<>\\&\\'\\\"\\/]");
    return std::regex_replace(input, dangerous_chars, "");
}

// Helper function to compress response data (simple implementation)
bool shouldCompressResponse(size_t contentSize, const std::string& contentType) {
    // Only compress responses larger than 1KB and of text types
    return contentSize > 1024 && 
           (contentType.find("text/") != std::string::npos || 
            contentType.find("application/json") != std::string::npos);
}

// Simple circuit breaker implementation
class CircuitBreaker {
private:
    std::atomic<int> failureCount{0};
    std::atomic<bool> circuitOpen{false};
    std::chrono::time_point<std::chrono::steady_clock> resetTime;
    const int threshold = 5; // Number of failures before opening circuit
    const std::chrono::seconds resetTimeout{30}; // Time before attempting reset

public:
    bool isOpen() const {
        if (circuitOpen) {
            auto now = std::chrono::steady_clock::now();
            if (now > resetTime) {
                // Allow a trial call after timeout
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
	
	// Circuit breakers for different operations
	CircuitBreaker shortestPathCircuit;
	CircuitBreaker primePathCircuit;

	// Set adaptive timeouts for read and write operations to handle variable latency
	// Longer timeout for complex operations, shorter for simple ones
	svr.set_read_timeout(10, 0);  // 10 seconds for read (client might be on slow connection)
	svr.set_write_timeout(10, 0); // 10 seconds for write
	
	// Set payload size limits to manage bandwidth usage
	const size_t MAX_PAYLOAD_SIZE = 5 * 1024 * 1024; // 5MB general limit
	const size_t MAX_GRAPH_SIZE = 10 * 1024 * 1024;  // 10MB for graph initialization
	
	// Add content type negotiation for heterogeneous networks
	std::vector<std::string> supportedContentTypes = {"text/plain", "application/json"};
	
	// Add server identification with version for service discovery
	const std::string SERVER_ID = "GraphQueryServer/1.0";
	
	// Add request rate limiting to prevent resource exhaustion
	std::unordered_map<std::string, std::pair<int, std::chrono::time_point<std::chrono::steady_clock>>> requestCounts;
	const int MAX_REQUESTS_PER_MINUTE = 10;
	const int MAX_PAYLOAD_REQUESTS_PER_MINUTE = 10; // For large payload requests
	
	// Enhanced exception handler to gracefully handle network and processing exceptions
	svr.set_exception_handler([&](const httplib::Request& req, httplib::Response& res, std::exception_ptr ep) {
		
		
		try {
			std::rethrow_exception(ep);
		} catch (const std::ios_base::failure& e) {
			// Handle I/O errors (network issues)
			std::cerr << "Network I/O error during request processing: " << e.what() << std::endl;
			res.status = 503; // Service Unavailable
			res.set_header("Retry-After", "30"); // Suggest retry after 30 seconds
			res.set_content("Service temporarily unavailable due to network issues", "text/plain");
		} catch (const std::system_error& e) {
			// Handle system errors (including network-related ones)
			std::cerr << "System error during request processing: " << e.what() << 
			          " (code: " << e.code() << ")" << std::endl;
			res.status = 500;
			res.set_content("System error: " + std::string(e.what()), "text/plain");
		} catch (const std::runtime_error& e) {
			// Handle runtime errors
			std::cerr << "Runtime error during request processing: " << e.what() << std::endl;
			res.status = 500;
			res.set_content("Server error: " + std::string(e.what()), "text/plain");
		} catch (const std::exception& e) {
			// Handle other exceptions
			std::cerr << "Exception during request processing: " << e.what() << std::endl;
			res.status = 500;
			// Sanitize error message to prevent information disclosure
			std::string sanitized_error = sanitizeInput(e.what());
			res.set_content("Server error: " + sanitized_error, "text/plain");
		} catch (...) {
			std::cerr << "Unknown exception during request processing" << std::endl;
			res.status = 500;
			res.set_content("Internal server error", "text/plain");
		}
		
		// Add server identification for troubleshooting
		res.set_header("Server", SERVER_ID);
		
		// Add cache control headers to prevent caching of error responses
		res.set_header("Cache-Control", "no-store, must-revalidate");
		res.set_header("Pragma", "no-cache");
	});
	
	// Enhanced error handler for HTTP errors with better client guidance
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
				res.set_header("Retry-After", "60"); // Suggest retry after 60 seconds
				break;
			case 500: 
				message = "Internal Server Error"; 
				additional_info = "The server encountered an unexpected condition. Please report this issue."; 
				break;
			case 503: 
				message = "Service Unavailable"; 
				additional_info = "The server is currently unable to handle the request due to temporary overloading or maintenance."; 
				res.set_header("Retry-After", "120"); // Suggest retry after 120 seconds
				break;
			case 507: 
				message = "Insufficient Storage"; 
				additional_info = "The server has insufficient storage to complete the request. Try with a smaller graph."; 
				break;
		}
		
		// Create a more informative error response
		std::string response_body = message;
		if (!additional_info.empty()) {
			response_body += ": " + additional_info;
		}
		
		// Add request ID for tracking
		std::random_device rd;
		std::mt19937 gen(rd());
		std::uniform_int_distribution<> dis(10000000, 99999999);
		std::string request_id = std::to_string(dis(gen));
		
		res.set_header("X-Request-ID", request_id);
		res.set_header("Server", SERVER_ID);
		res.set_content(response_body, "text/plain");
		
		// Add cache control headers to prevent caching of error responses
		res.set_header("Cache-Control", "no-store, must-revalidate");
		res.set_header("Pragma", "no-cache");
	});

	// Enhanced heartbeat endpoint for service discovery and health monitoring
	// Tracks server start time for uptime calculation
	auto server_start_time = std::chrono::steady_clock::now();
	
	svr.Get("/heartbeat", [&](const httplib::Request& req, httplib::Response& res) {
		cout << "Received request: " << req.path << " from " << req.remote_addr << endl;
		
		// Apply rate limiting
		auto now = std::chrono::steady_clock::now();
		auto& client_info = requestCounts[req.remote_addr];
		
		// Reset counter if it's been more than a minute
		if (now - client_info.second > std::chrono::minutes(1)) {
			client_info.first = 0;
			client_info.second = now;
		}
		
		// Increment request count
		client_info.first++;
		
		// Check if rate limit exceeded
		if (client_info.first > MAX_REQUESTS_PER_MINUTE) {
			res.status = 429; // Too Many Requests
			res.set_header("Retry-After", "60");
			res.set_content("Rate limit exceeded. Please try again later.", "text/plain");
			return;
		}
		
		try {
			// Calculate server uptime
			auto uptime_duration = std::chrono::steady_clock::now() - server_start_time;
			auto uptime_hours = std::chrono::duration_cast<std::chrono::hours>(uptime_duration).count();
			auto uptime_minutes = std::chrono::duration_cast<std::chrono::minutes>(uptime_duration).count() % 60;
			auto uptime_seconds = std::chrono::duration_cast<std::chrono::seconds>(uptime_duration).count() % 60;
			
			// Build status response with detailed information
			std::stringstream status;
			status << "Status: Running\n"
			       << "Server ID: " << SERVER_ID << "\n"
			       << "Uptime: " << uptime_hours << "h " << uptime_minutes << "m " << uptime_seconds << "s\n"
			       << "Endpoints: /heartbeat, /graph_info, /initialize, /shortest_path, /prime_path\n"
			       << "Content types: " << supportedContentTypes[0];
			
			for (size_t i = 1; i < supportedContentTypes.size(); i++) {
				status << ", " << supportedContentTypes[i];
			}
			
			// Add circuit breaker status
			status << "\nShortestPath service: " << (shortestPathCircuit.isOpen() ? "degraded" : "available");
			status << "\nPrimePath service: " << (primePathCircuit.isOpen() ? "degraded" : "available");
			
			// Set response headers for service discovery
			res.set_header("Server", SERVER_ID);
			res.set_header("X-Content-Type-Options", "nosniff");
			res.set_header("Cache-Control", "no-cache, max-age=0");
			
			// Determine content type based on Accept header
			std::string content_type = "text/plain";
			if (req.has_header("Accept") && req.get_header_value("Accept").find("application/json") != std::string::npos) {
				// Return JSON format if requested
				content_type = "application/json";
				std::string json = "{\"status\":\"running\",\"server_id\":\"" + SERVER_ID + "\","
				                 "\"uptime_seconds\":" + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(uptime_duration).count()) + ","
				                 "\"shortest_path_available\":" + std::string(shortestPathCircuit.isOpen() ? "false" : "true") + ","
				                 "\"prime_path_available\":" + std::string(primePathCircuit.isOpen() ? "false" : "true") + "}";
				res.set_content(json, content_type);
			} else {
				// Return plain text by default
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

		// Apply rate limiting
		auto now = std::chrono::steady_clock::now();
		auto& client_info = requestCounts[req.remote_addr];
		
		// Reset counter if it's been more than a minute
		if (now - client_info.second > std::chrono::minutes(1)) {
			client_info.first = 0;
			client_info.second = now;
		}
		
		// Increment request count
		client_info.first++;
		
		// Check if rate limit exceeded
		if (client_info.first > MAX_REQUESTS_PER_MINUTE) {
			res.status = 429; // Too Many Requests
			res.set_header("Retry-After", "60");
			res.set_content("Rate limit exceeded. Please try again later.", "text/plain");
			return;
		}

		try {
			// Start a timer to track operation duration
			auto start_time = std::chrono::high_resolution_clock::now();
			
			// Call graph.printGraphInfo() to console
			graph.printGraphInfo();
			
			// Calculate operation duration
			auto end_time = std::chrono::high_resolution_clock::now();
			auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
			cout << "Graph info printed in " << duration << "ms" << endl;
			
			// Set response headers
			res.set_header("Server", SERVER_ID);
			res.set_header("X-Processing-Time", std::to_string(duration) + "ms");
			
			// Content negotiation based on Accept header
			std::string content_type = "text/plain";
			std::string response_content;
			
			if (req.has_header("Accept") && req.get_header_value("Accept").find("application/json") != std::string::npos) {
				// Return JSON format
				content_type = "application/json";
				response_content = "{\"message\":\"Graph information printed to server console.\",\"duration_ms\":" + 
				                   std::to_string(duration) + "}";
			} else {
				// Return plain text by default
				response_content = "Graph information printed to server console. (Processed in " + 
				                   std::to_string(duration) + "ms)";
			}
			
			// Set appropriate cache control headers
			res.set_header("Cache-Control", "no-cache, max-age=0");
			
			// Check if response should be compressed
			if (shouldCompressResponse(response_content.size(), content_type)) {
				// In a real implementation, we would compress the content here
				// For now, just indicate that compression would be applied
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
		
		// Apply stricter rate limiting for large payload requests
		auto now = std::chrono::steady_clock::now();
		auto& client_info = requestCounts[req.remote_addr];
		
		// Reset counter if it's been more than a minute
		if (now - client_info.second > std::chrono::minutes(1)) {
			client_info.first = 0;
			client_info.second = now;
		}
		
		// Increment request count
		client_info.first++;
		
		// Check if rate limit exceeded for large payload operations
		if (client_info.first > MAX_PAYLOAD_REQUESTS_PER_MINUTE) {
			res.status = 429; // Too Many Requests
			res.set_header("Retry-After", "60");
			res.set_header("X-RateLimit-Limit", std::to_string(MAX_PAYLOAD_REQUESTS_PER_MINUTE));
			res.set_header("X-RateLimit-Remaining", "0");
			res.set_header("X-RateLimit-Reset", std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::minutes(1) - (now - client_info.second)).count()));
			res.set_content("Rate limit exceeded for large payload operations. Please try again later.", "text/plain");
			return;
		}
		
		// Check if the request body is too large (implement reasonable size limit)
		if (req.body.size() > MAX_GRAPH_SIZE) {
			res.status = 413; // Payload Too Large
			res.set_header("X-Max-Payload-Size", std::to_string(MAX_GRAPH_SIZE));
			res.set_content("Request body too large. Maximum allowed size is " + 
			               std::to_string(MAX_GRAPH_SIZE/1024/1024) + "MB", "text/plain");
			return;
		}
		
		// Validate input format (basic check)
		if (req.body.empty()) {
			res.status = 400; // Bad Request
			res.set_content("Empty graph data provided. Please provide valid graph data.", "text/plain");
			return;
		}
		
		// Check content type for proper format validation
		std::string content_type = req.has_header("Content-Type") ? req.get_header_value("Content-Type") : "";
		if (!content_type.empty() && 
			content_type.find("text/plain") == std::string::npos && 
			content_type.find("application/octet-stream") == std::string::npos) {
			res.status = 415; // Unsupported Media Type
			res.set_content("Unsupported content type. Please provide graph data as text/plain or application/octet-stream.", "text/plain");
			return;
		}
		
		// Perform basic format validation before processing
		bool valid_format = false;
		size_t node_count = 0;
		size_t edge_count = 0;
		
		// Check if the data contains at least one valid node or edge definition
		std::istringstream iss(req.body);
		std::string line;
		while (std::getline(iss, line)) {
			// Trim whitespace
			line = Graph::trim(line);
			if (line.empty()) continue;
			
			if (line[0] == '*') {
				// Node definition
				valid_format = true;
				node_count++;
			} else if (line[0] == '-') {
				// Edge definition
				valid_format = true;
				edge_count++;
			}
		}
		
		if (!valid_format) {
			res.status = 400; // Bad Request
			res.set_content("Invalid graph format. Graph data must contain node definitions (lines starting with '*') " 
						  "and/or edge definitions (lines starting with '-').", "text/plain");
			return;
		}
		
		// Sanitize input to prevent injection attacks
		std::string sanitized_body = sanitizeInput(req.body);
		
		try {
			// Start a timer to track operation duration
			auto start_time = std::chrono::high_resolution_clock::now();
			
			// Call your Graph class function to parse the graph file data from the request body
			graph.parseGraphFile(sanitized_body);
			
			// Calculate operation duration
			auto end_time = std::chrono::high_resolution_clock::now();
			auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
			cout << "Graph initialization completed in " << duration << "ms" << endl;
			
			// Get actual node and edge counts after parsing
			size_t actual_nodes = graph.getNodesCount();
			size_t actual_edges = graph.getEdgesCount();
			
			// Set response headers
			res.set_header("Server", SERVER_ID);
			res.set_header("X-Processing-Time", std::to_string(duration) + "ms");
			res.set_header("X-Graph-Nodes", std::to_string(actual_nodes));
			res.set_header("X-Graph-Edges", std::to_string(actual_edges));
			
			// Content negotiation based on Accept header
			std::string response_content_type = "text/plain";
			std::string response_content;
			
			if (req.has_header("Accept") && req.get_header_value("Accept").find("application/json") != std::string::npos) {
				// Return JSON format
				response_content_type = "application/json";
				response_content = "{\"status\":\"success\",\"message\":\"Graph initialized successfully\","
								 "\"nodes\":" + std::to_string(actual_nodes) + ","
								 "\"edges\":" + std::to_string(actual_edges) + ","
								 "\"duration_ms\":" + std::to_string(duration) + "}";
			} else {
				// Return plain text by default
				response_content = "Graph initialized successfully:\n" +
								std::string("- Nodes: ") + std::to_string(actual_nodes) + "\n" +
								 "- Edges: " + std::to_string(actual_edges) + "\n" +
								 "- Processing time: " + std::to_string(duration) + "ms";
			}
			
			// Set appropriate cache control headers
			res.set_header("Cache-Control", "no-cache, max-age=0");
			
			// Check if response should be compressed
			if (shouldCompressResponse(response_content.size(), response_content_type)) {
				// In a real implementation, we would compress the content here
				res.set_header("X-Compression-Applied", "would-be-gzip");
				res.set_header("Content-Encoding", "gzip");
			}
			
			res.set_content(response_content, response_content_type);
		} catch (const std::exception& e) {
			// Handle any exceptions that might occur during graph initialization
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

		// Check circuit breaker status first
		if (shortestPathCircuit.isOpen()) {
			res.status = 503; // Service Unavailable
			res.set_header("Retry-After", "30");
			res.set_content("Shortest path service is temporarily unavailable. Please try again later.", "text/plain");
			return;
		}

		// Apply rate limiting
		auto now = std::chrono::steady_clock::now();
		auto& client_info = requestCounts[req.remote_addr];
		
		// Reset counter if it's been more than a minute
		if (now - client_info.second > std::chrono::minutes(1)) {
			client_info.first = 0;
			client_info.second = now;
		}
		
		// Increment request count
		client_info.first++;
		
		// Check if rate limit exceeded
		if (client_info.first > MAX_REQUESTS_PER_MINUTE) {
			res.status = 429; // Too Many Requests
			res.set_header("Retry-After", "60");
			res.set_content("Rate limit exceeded. Please try again later.", "text/plain");
			return;
		}

		// Check payload size
		if (req.body.size() > MAX_PAYLOAD_SIZE) {
			res.status = 413; // Payload Too Large
			res.set_content("Request body too large", "text/plain");
			return;
		}

		// Use istringstream to parse the request body
		istringstream iss(req.body);
		string start_node, end_node;
		iss >> start_node >> end_node;

		// Validate input
		if (start_node.empty() || end_node.empty()) {
			res.status = 400;  // Bad Request
			res.set_content("Invalid input: start_node and end_node required", "text/plain");
			return;
		}

		// Sanitize input to prevent injection attacks
		start_node = sanitizeInput(start_node);
		end_node = sanitizeInput(end_node);

		try {
			// Start a timer to track operation duration
			auto start_time = std::chrono::high_resolution_clock::now();
			
			// Call the Graph method to find the shortest path
			string result = graph.findShortestPathParallel(start_node, end_node);
			
			// Calculate operation duration
			auto end_time = std::chrono::high_resolution_clock::now();
			auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
			cout << "Shortest path calculation completed in " << duration << "ms" << endl;
			
			// Check if the operation took too long (potential timeout situation)
			if (duration > 9000) { // 9 seconds (close to the 10-second timeout)
				cout << "Warning: Shortest path calculation approaching timeout threshold" << endl;
			}
			
			// Record success in circuit breaker
			shortestPathCircuit.recordSuccess();
			
			// Set response headers
			res.set_header("Server", SERVER_ID);
			res.set_header("X-Processing-Time", std::to_string(duration) + "ms");
			
			// Content negotiation based on Accept header
			std::string content_type = "text/plain";
			std::string response_content;
			
			if (req.has_header("Accept") && req.get_header_value("Accept").find("application/json") != std::string::npos) {
				// Return JSON format
				content_type = "application/json";
				response_content = "{\"path\":\"" + result + "\",\"duration_ms\":" + 
				                   std::to_string(duration) + "}";
			} else {
				// Return plain text by default
				response_content = result;
			}
			
			// Check if response should be compressed
			if (shouldCompressResponse(response_content.size(), content_type)) {
				// In a real implementation, we would compress the content here
				res.set_header("X-Compression-Applied", "would-be-gzip");
			}
			
			res.set_content(response_content, content_type);
		} catch (const std::exception& e) {
			// Record failure in circuit breaker
			shortestPathCircuit.recordFailure();
			
			// Handle any exceptions that might occur during path calculation
			std::cerr << "Error during shortest path calculation: " << e.what() << std::endl;
			res.status = 500;
			res.set_content("Failed to calculate shortest path: " + sanitizeInput(e.what()), "text/plain");
		} catch (...) {
			// Record failure in circuit breaker
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

		// Check circuit breaker status first
		if (primePathCircuit.isOpen()) {
			res.status = 503; // Service Unavailable
			res.set_header("Retry-After", "30");
			res.set_content("Prime path service is temporarily unavailable. Please try again later.", "text/plain");
			return;
		}

		// Apply rate limiting
		auto now = std::chrono::steady_clock::now();
		auto& client_info = requestCounts[req.remote_addr];
		
		// Reset counter if it's been more than a minute
		if (now - client_info.second > std::chrono::minutes(1)) {
			client_info.first = 0;
			client_info.second = now;
		}
		
		// Increment request count
		client_info.first++;
		
		// Check if rate limit exceeded
		if (client_info.first > MAX_REQUESTS_PER_MINUTE) {
			res.status = 429; // Too Many Requests
			res.set_header("Retry-After", "60");
			res.set_content("Rate limit exceeded. Please try again later.", "text/plain");
			return;
		}

		// Check payload size
		if (req.body.size() > MAX_PAYLOAD_SIZE) {
			res.status = 413; // Payload Too Large
			res.set_content("Request body too large", "text/plain");
			return;
		}

		// Use istringstream to parse the request body
		istringstream iss(req.body);
		string start_node, end_node;
		iss >> start_node >> end_node;

		// Validate input
		if (start_node.empty() || end_node.empty()) {
			res.status = 400;  // Bad Request
			res.set_content("Invalid input: start_node and end_node required", "text/plain");
			return;
		}

		// Sanitize input to prevent injection attacks
		start_node = sanitizeInput(start_node);
		end_node = sanitizeInput(end_node);

		try {
			// Start a timer to track operation duration
			auto start_time = std::chrono::high_resolution_clock::now();
			
			// Call the Graph method to find the prime path
			string result = graph.findPrimePathParallel(start_node, end_node);


			
			// Calculate operation duration
			auto end_time = std::chrono::high_resolution_clock::now();
			auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
			cout << "Prime path calculation completed in " << duration << "ms" << endl;
			
			// Check if the operation took too long (potential timeout situation)
			if (duration > 9000) { // 9 seconds (close to the 10-second timeout)
				cout << "Warning: Prime path calculation approaching timeout threshold" << endl;
			}
			
			// Record success in circuit breaker
			primePathCircuit.recordSuccess();
			
			// Set response headers
			res.set_header("Server", SERVER_ID);
			res.set_header("X-Processing-Time", std::to_string(duration) + "ms");
			
			// Content negotiation based on Accept header
			std::string content_type = "text/plain";
			std::string response_content;
			
			if (req.has_header("Accept") && req.get_header_value("Accept").find("application/json") != std::string::npos) {
				// Return JSON format
				content_type = "application/json";
				response_content = "{\"path\":\"" + result + "\",\"duration_ms\":" + 
				                   std::to_string(duration) + "}";
			} else {
				// Return plain text by default
				response_content = result;
			}
			
			// Check if response should be compressed
			if (shouldCompressResponse(response_content.size(), content_type)) {
				// In a real implementation, we would compress the content here
				res.set_header("X-Compression-Applied", "would-be-gzip");
			}
			
			res.set_content(response_content, content_type);
		} catch (const std::exception& e) {
			// Record failure in circuit breaker
			primePathCircuit.recordFailure();
			
			// Handle any exceptions that might occur during path calculation
			std::cerr << "Error during prime path calculation: " << e.what() << std::endl;
			res.status = 500;
			res.set_content("Failed to calculate prime path: " + sanitizeInput(e.what()), "text/plain");
		} catch (...) {
			// Record failure in circuit breaker
			primePathCircuit.recordFailure();
			
			std::cerr << "Unknown error during prime path calculation" << std::endl;
			res.status = 500;
			res.set_content("Failed to calculate prime path: Unknown error", "text/plain");
		}
	});

	// Set connection timeout
	svr.set_keep_alive_max_count(100); // Maximum number of keep-alive connections
	
	
	cout << "Server started at http://localhost:8080" << endl;
	cout << "Press Ctrl+C to stop the server" << endl;
	
	//// Add service discovery endpoint for topology changes
	//svr.Get("/service_discovery", [&](const httplib::Request& req, httplib::Response& res) {
	//	// Return service information in a standardized format
	//	std::string content_type = "application/json";
	//	
	//	// Create a service descriptor with all necessary connection information
	//	std::string service_info = "{\"service\":\"GraphQueryServer\","
	//	                         "\"version\":\"1.0\","
	//	                         "\"endpoints\":["
	//	                         "{\"path\":\"/heartbeat\",\"method\":\"GET\",\"description\":\"Server health check\"},"
	//	                         "{\"path\":\"/graph_info\",\"method\":\"GET\",\"description\":\"Get graph information\"},"
	//	                         "{\"path\":\"/initialize\",\"method\":\"POST\",\"description\":\"Initialize graph structure\"},"
	//	                         "{\"path\":\"/shortest_path\",\"method\":\"POST\",\"description\":\"Find shortest path between nodes\"},"
	//	                         "{\"path\":\"/prime_path\",\"method\":\"POST\",\"description\":\"Find path with prime weight between nodes\"},"
	//	                         "{\"path\":\"/service_discovery\",\"method\":\"GET\",\"description\":\"Service discovery endpoint\"}"
	//	                         "],"
	//	                         "\"status\":\"available\","
	//	                         "\"content_types\":[\"text/plain\",\"application/json\"],"
	//	                         "\"host\":\"localhost\","
	//	                         "\"port\":8080}";
	//	
	//	res.set_header("Server", SERVER_ID);
	//	res.set_header("Cache-Control", "max-age=60"); // Cache for 60 seconds
	//	res.set_content(service_info, content_type);
	//});
	
	// Log server startup information
	std::cout << "Starting " << SERVER_ID << " on http://localhost:8080" << std::endl;
	std::cout << "Available endpoints:" << std::endl;
	std::cout << "  - GET  /heartbeat" << std::endl;
	std::cout << "  - GET  /graph_info" << std::endl;
	std::cout << "  - POST /initialize" << std::endl;
	std::cout << "  - POST /shortest_path" << std::endl;
	std::cout << "  - POST /prime_path" << std::endl;
	std::cout << "  - GET  /service_discovery" << std::endl;
	
	// Start the server
	if (!svr.listen("0.0.0.0", 8080)) {
		std::cerr << "Failed to start server!" << std::endl;
		return 1;
	}
	
	return 0;
}