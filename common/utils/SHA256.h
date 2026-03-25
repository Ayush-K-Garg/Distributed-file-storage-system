#ifndef SHA256_H
#define SHA256_H

#include <string>
#include <vector>
#include <fstream>
#include <iomanip>
#include <sstream>

class SHA256 {
public:
    static std::string hashFile(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return "";
        
        // Lightweight FNV-1a or simplified hash 
       
        unsigned int hash = 2166136261u;
        char buffer[4096];
        while (file.read(buffer, sizeof(buffer))) {
            for (int i = 0; i < file.gcount(); ++i) {
                hash ^= (unsigned char)buffer[i];
                hash *= 16777619u;
            }
        }
        std::stringstream ss;
        ss << std::hex << std::setw(8) << std::setfill('0') << hash;
        return ss.str();
    }
};

#endif