#include "FileChunker.h"
#include <fstream>

FileChunker::FileChunker(size_t chunkSize) : chunkSize(chunkSize) {}

std::vector<Chunk> FileChunker::split(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    std::vector<Chunk> chunks;

    if (!file) {
        return chunks;
    }

    int id = 0;

    while (!file.eof()) {
        std::vector<char> buffer(chunkSize);

        file.read(buffer.data(), chunkSize);
        std::streamsize bytesRead = file.gcount();

        buffer.resize(bytesRead);

        if (bytesRead > 0) {
            chunks.emplace_back(id++, buffer);
        }
    }

    return chunks;
}

void FileChunker::merge(const std::string& outputPath,
                       const std::vector<Chunk>& chunks) {
    std::ofstream file(outputPath, std::ios::binary);

    for (const auto& chunk : chunks) {
        file.write(chunk.data.data(), chunk.data.size());
    }
}