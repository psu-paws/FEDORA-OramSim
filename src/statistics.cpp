#include <statistics.hpp>

void 
MemoryStatistics::clear() {
    this->read_requests = 0;
    this->write_requests = 0;
    
    this->read_bytes_requested = 0;
    this->write_bytes_requested = 0;

    this->bytes_read = 0;
    this->bytes_wrote = 0;
}

void 
MemoryStatistics::record_request(const MemoryRequest &request) {
    switch(request.type) {
        case MemoryRequestType::READ: {
            this->read_requests++;
            this->read_bytes_requested += request.size;
            break;
        }
        case MemoryRequestType::WRITE: {
            this->write_requests++;
            this->write_bytes_requested += request.size;
            break;
        }
        case MemoryRequestType::READ_WRITE: {
            this->read_requests++;
            this->write_requests++;
            this->read_bytes_requested += request.size;
            this->write_bytes_requested += request.size;
            break;
        }
        default: {} // TODO: add tracking for this, currently ignores other requests
    }
}

void 
MemoryStatistics::add_read_write(uint64_t bytes_read, uint64_t bytes_wrote) {
    this->bytes_read += bytes_read;
    this->bytes_wrote += bytes_wrote;
}

toml::table 
MemoryStatistics::to_toml() const{
    auto table = toml::table{
        {"read_requests", this->read_requests},
        {"write_requests", this->write_requests},
        {"read_bytes_requested", this->read_bytes_requested},
        {"write_bytes_requested", this->write_bytes_requested},
        {"bytes_read", this->bytes_read},
        {"bytes_wrote", this->bytes_wrote}
    };

    return table;
}

void 
MemoryStatistics::from_toml(const toml::table &table) {
    this->read_requests = table["read_requests"].value<int64_t>().value();
    this->write_requests = table["write_requests"].value<int64_t>().value();
    this->read_bytes_requested = table["read_bytes_requested"].value<int64_t>().value();
    this->write_bytes_requested = table["write_bytes_requested"].value<int64_t>().value();
    this->bytes_read = table["bytes_read"].value<int64_t>().value();
    this->bytes_wrote = table["bytes_wrote"].value<int64_t>().value();
}