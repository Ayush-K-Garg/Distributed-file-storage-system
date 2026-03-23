#pragma once
#include "../interfaces/IChunker.h"

class FileChunker : public IChunker {
private:
    size_t chunkSize;

public:
    FileChunker(size_t chunkSize);
    
    std::vector<Chunk> split(const std::string& filePath) override;
    void merge(const std::string& outputPath,
               const std::vector<Chunk>& chunks) override;
};