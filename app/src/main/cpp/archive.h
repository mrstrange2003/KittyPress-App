// archive.h
#pragma once
#include <string>
#include <vector>
#include "progress.h"

struct ArchiveInput {
    std::string absPath;  // actual disk path
    std::string relPath;  // path inside archive
    std::string ext;      // stored extension (without leading dot), may be empty
};

void createArchive(const std::vector<std::string>& inputs,
                   const std::string& outputArchive);

std::string extractArchive(const std::string& archivePath, const std::string& outputFolder);
