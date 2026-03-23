#pragma once
#include <vector>
#include <string>
#include "../models/Chunk.h"

class IChunker {
public:
    virtual std::vector<Chunk> split(const std::string& filePath) = 0;
    virtual void merge(const std::string& outputPath,
                       const std::vector<Chunk>& chunks) = 0;

    virtual ~IChunker() = default;
};