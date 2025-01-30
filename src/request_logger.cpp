#include "request_logger.hpp"
#include <absl/strings/str_format.h>
#include <fstream>
RequestLogger::RequestLogger(std::string_view name, uint64_t size) :size(size), log_path(absl::StrFormat("%s.log", name))
{}

void 
RequestLogger::start_logging(bool append) {
    std::ios_base::openmode open_mode = std::ios_base::out;
    if (append) {
        open_mode |= std::ios_base::app;
    }
    this->output.reset(new std::ofstream(this->log_path, open_mode));
    (*this->output) << absl::StrFormat("#|size %lu\n", size);
}

void 
RequestLogger::log_request(const MemoryRequest &request) {
     if (this->output) {
        if (request.type == MemoryRequestType::READ_WRITE) {
            // read_write requests are logged as two requests
            // a read request immediately followed by a write request.
            // std::string data_as_string;

            // for (const unsigned char &data : request.data) {
            //     absl::StrAppendFormat(&data_as_string, "%02x", data);
            // }

            // std::cout << "Read Write Request detected!\n";

            // std::string output = absl::StrFormat("%010lu R %d 0x%08x %s\n", RequestLogger::log_count, request.size, request.address, data_as_string);
            std::string output = absl::StrFormat("%010lu R %d 0x%08x\n", RequestLogger::log_count, request.size, request.address);
            RequestLogger::log_count++;
            (*this->output) << output;
        }
        std::string operation_type;
        switch (request.type) {
            // since this is not backed we do nothing.
            case MemoryRequestType::READ:
            operation_type = "R";
            break;

            case MemoryRequestType::WRITE:
            operation_type = "W";
            break;

            case MemoryRequestType::READ_WRITE:
            operation_type = "W";
            break;

            default:
            throw std::invalid_argument("unkown memory request type");
        }

        // std::string data_as_string;

        // for (const unsigned char &data : request.data) {
        //     absl::StrAppendFormat(&data_as_string, "%02x", data);
        // }

        // std::string output = absl::StrFormat("%010lu %s %d 0x%08x %s\n", RequestLogger::log_count, operation_type, request.size, request.address, data_as_string);

        // uncomment this line below to not log the data
        std::string output = absl::StrFormat("%010lu %s %d 0x%08x\n", RequestLogger::log_count, operation_type, request.size, request.address);
        RequestLogger::log_count++;
        (*this->output) << output;
    }
}

void 
RequestLogger::barrier() {
    if (this->output) {
        std::string output = absl::StrFormat("%010lu %s %d 0x%08x %s\n", RequestLogger::log_count, "B", 1, 0, "");
        *(this->output) << output;
        RequestLogger::log_count ++;
    }
}

void 
RequestLogger::stop_logging() {
    this->output.reset();
}

uint64_t RequestLogger::log_count = 0;