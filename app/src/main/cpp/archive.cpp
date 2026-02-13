// archive.cpp
#include "archive.h"
#include "compress.h"   // streaming compress/decompress helpers
#include "kitty.h"
#include "progress.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <future>
#include <thread>
#include <atomic>

using namespace std;
namespace fs = std::filesystem;

static void gatherFiles(const fs::path& base, const fs::path& p,
                        vector<ArchiveInput>& list) {

    if (fs::is_directory(p)) {
        for (auto& e : fs::recursive_directory_iterator(p)) {
            if (fs::is_regular_file(e.path())) {
                string ext = e.path().extension().string();
                if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);

                list.push_back({
                                       e.path().string(),
                                       fs::relative(e.path(), base).string(),
                                       ext
                               });
            }
        }
    } else if (fs::is_regular_file(p)) {

        string name = p.filename().string();
        string ext = p.extension().string();
        if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);

        list.push_back({
                               p.string(),
                               name,
                               ext
                       });
    }
}

void createArchive(const vector<string>& inputs, const string& outputArchive) {
    vector<ArchiveInput> files;
    for (auto& in : inputs)
        gatherFiles(fs::absolute(in).parent_path(), fs::absolute(in), files);

    // compute total original size for progress reporting (copy phase)
    // keep this as original behavior so native_progress_set_total reflects 'copying' bytes
    uint64_t totalOrig = 0;
    for (auto &f : files) {
        try {
            if (fs::exists(f.absPath)) {
                totalOrig += (uint64_t)fs::file_size(f.absPath);
            }
        } catch (...) { }
    }
    native_progress_set_total(totalOrig);

    ofstream out(outputArchive, ios::binary);
    if (!out) throw runtime_error("Cannot open output archive");

    // overall archive magic & version
    out.write(KITTY_MAGIC.c_str(), (streamsize)KITTY_MAGIC.size());
    uint8_t ver = KITTY_VERSION;
    out.write(reinterpret_cast<const char*>(&ver), 1);

    uint32_t count = (uint32_t)files.size();
    out.write(reinterpret_cast<const char*>(&count), 4);

    cout << "Creating archive with " << count << " file(s)\n";

    for (auto& f : files) {
        uint16_t pathLen = (uint16_t)f.relPath.size();
        uint8_t flags = 1;
        uint64_t origSize = 0;
        try { origSize = (uint64_t)fs::file_size(f.absPath); } catch (...) { origSize = 0; }
        uint64_t dataSize = 0; // will be patched after streaming KP05 payload
        uint16_t extLen = (uint16_t)f.ext.size();

        // Entry header
        out.write(reinterpret_cast<const char*>(&pathLen), 2);
        out.write(f.relPath.c_str(), pathLen);
        out.write(reinterpret_cast<const char*>(&flags), 1);
        out.write(reinterpret_cast<const char*>(&origSize), 8);

        // Reserve space for dataSize (we don't know it yet)
        std::streampos dataSizePos = out.tellp();
        out.write(reinterpret_cast<const char*>(&dataSize), 8); // placeholder

        // Store extension (no leading dot)
        out.write(reinterpret_cast<const char*>(&extLen), 2);
        if (extLen > 0) out.write(f.ext.c_str(), extLen);

        // Now stream KP05 payload directly into the archive (no .tmpkitty)
        uint64_t payloadSize = 0;
        // compressToStream writes a KP05-wrapped payload starting at current stream pos
        compressToStream(f.absPath, out, payloadSize);

        // Patch dataSize with actual payload size
        std::streampos endPos = out.tellp();
        dataSize = payloadSize;
        out.seekp(dataSizePos);
        out.write(reinterpret_cast<const char*>(&dataSize), 8);
        out.seekp(endPos);

        cout << "  + " << f.relPath << " (" << origSize << " → " << dataSize << ")\n";
    }

    out.close();
    cout << "Archive created: " << outputArchive << endl;
}


std::string extractArchive(const std::string& archivePath, const std::string& outputFolder) {
    std::ifstream in(archivePath, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open archive");

    std::string magic(KITTY_MAGIC.size(), '\0');
    in.read(&magic[0], (streamsize)magic.size());
    if (magic != KITTY_MAGIC)
        throw std::runtime_error("Not a KP05 archive");

    uint8_t ver;
    in.read(reinterpret_cast<char*>(&ver), 1);
    if (ver != KITTY_VERSION) {
        // for now we only support KP05 archives
        throw std::runtime_error("Unsupported archive version");
    }

    uint32_t count;
    in.read(reinterpret_cast<char*>(&count), 4);

    struct Entry {
        std::string rel;
        std::string ext;        // stored extension (may be empty)
        uint8_t flags;
        uint64_t origSize;
        uint64_t dataSize;      // size of KP05 payload in archive
        uint64_t payloadOffset; // file offset where KP05 payload begins
    };

    std::vector<Entry> entries;
    entries.reserve(count);
    std::vector<std::string> relPaths;
    relPaths.reserve(count);

    uint64_t totalCompressed = 0;

    // First pass: read all headers, record payload offsets, skip payloads
    for (uint32_t i = 0; i < count; ++i) {
        uint16_t pathLen;
        in.read(reinterpret_cast<char*>(&pathLen), 2);

        std::string rel(pathLen, '\0');
        in.read(&rel[0], pathLen);

        uint8_t flags;
        in.read(reinterpret_cast<char*>(&flags), 1);

        uint64_t origSize = 0, dataSize = 0;
        in.read(reinterpret_cast<char*>(&origSize), 8);
        in.read(reinterpret_cast<char*>(&dataSize), 8);

        // read stored extension
        uint16_t extLen = 0;
        in.read(reinterpret_cast<char*>(&extLen), 2);
        std::string ext(extLen, '\0');
        if (extLen > 0) in.read(&ext[0], extLen);

        // remember where the KP05 payload starts
        std::streampos payloadPos = in.tellg();
        if (payloadPos == std::streampos(-1)) {
            throw std::runtime_error("Invalid payload position while reading archive");
        }

        // skip payload
        if (dataSize > 0) {
            in.seekg((std::streamoff)dataSize, std::ios::cur);
            if (!in.good()) {
                throw std::runtime_error("Unexpected EOF while skipping entry payload");
            }
        }

        entries.push_back({
                                  rel,
                                  ext,
                                  flags,
                                  origSize,
                                  dataSize,
                                  static_cast<uint64_t>(payloadPos)
                          });
        relPaths.push_back(rel);

        totalCompressed += dataSize;
    }

    // Set total compressed bytes for extraction progress
    native_progress_reset();
    native_progress_set_total(totalCompressed);

    // We'll batch progress updates to approx 1MB
    static const uint64_t PROGRESS_BATCH = 1024ull * 1024ull;
    uint64_t progressBatch = 0;

    // Decide extraction root
    std::string finalRootName;

    if (entries.empty()) {
        finalRootName = "KittyPress_Empty";
        in.close();
        return finalRootName;
    } else if (entries.size() == 1) {
        // single entry -> create single file named KittyPress_<filename.ext>
        fs::path p(relPaths[0]);
        std::string filename = p.filename().string();

        // if extension was stored, ensure filename has it
        if (!entries[0].ext.empty()) {
            p.replace_extension("." + entries[0].ext);
            filename = p.filename().string();
        }

        finalRootName = "KittyPress_" + filename;
        fs::path outRoot = fs::path(outputFolder) / finalRootName;
        fs::create_directories(outRoot.parent_path());

        auto &e = entries[0];
        fs::path outPath = fs::path(outputFolder) / finalRootName;

        // move to payload position
        in.seekg((std::streamoff)e.payloadOffset, std::ios::beg);
        if (!in.good()) {
            in.close();
            throw std::runtime_error("Failed to seek to payload");
        }

        // Decompress directly from archive stream (KP05 payload)
        decompressFromStream(in, e.dataSize, outPath.string());

        // report progress for this single entry
        progressBatch += e.dataSize;
        if (progressBatch > 0) {
            native_progress_add_processed(progressBatch);
            progressBatch = 0;
        }

        in.close();
        return finalRootName;
    } else {
        // multiple entries -> detect if all share a single top-level folder
        auto splitTop = [](const std::string &s) -> std::string {
            size_t pos = s.find('/');
            if (pos == std::string::npos) return "";
            return s.substr(0, pos);
        };

        auto starts_with = [](const std::string& s, const std::string& prefix) {
            return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
        };

        std::string firstTop = splitTop(relPaths[0]);
        bool allSameTop = !firstTop.empty();
        if (allSameTop) {
            for (size_t i = 1; i < relPaths.size(); ++i) {
                if (!starts_with(relPaths[i], firstTop + "/")) {
                    allSameTop = false;
                    break;
                }
            }
        }

        if (allSameTop) {
            finalRootName = "KittyPress_" + firstTop;
        } else {
            finalRootName = "KittyPress_Files";
        }
    }

    // Create extraction root directory
    fs::path rootOut = fs::path(outputFolder) / finalRootName;
    fs::create_directories(rootOut);

    // Prepare output paths first (single-threaded directory creation).
    std::vector<std::string> outPaths(entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        auto &e = entries[i];
        fs::path relp(e.rel);
        fs::path outPath = rootOut / relp;
        if (!e.ext.empty()) outPath.replace_extension("." + e.ext);
        fs::create_directories(outPath.parent_path());
        outPaths[i] = outPath.string();
    }

    // Multi-thread extraction by entry (safe: each task uses its own ifstream).
    const unsigned hw = std::thread::hardware_concurrency();
    const unsigned workers = std::max(1u, std::min(4u, hw == 0 ? 2u : hw));
    std::atomic<size_t> nextIndex{0};

    std::vector<std::future<void>> tasks;
    tasks.reserve(workers);

    for (unsigned w = 0; w < workers; ++w) {
        tasks.push_back(std::async(std::launch::async, [&, w]() {
            while (true) {
                size_t i = nextIndex.fetch_add(1);
                if (i >= entries.size()) break;

                const auto &e = entries[i];
                std::ifstream localIn(archivePath, std::ios::binary);
                if (!localIn) throw std::runtime_error("Cannot open archive worker stream");

                localIn.seekg((std::streamoff)e.payloadOffset, std::ios::beg);
                if (!localIn.good()) throw std::runtime_error("Failed to seek to payload");

                decompressFromStream(localIn, e.dataSize, outPaths[i]);
                native_progress_add_processed(e.dataSize);
            }
        }));
    }

    for (auto &t : tasks) t.get();

    in.close();
    return finalRootName; // return root folder name
}
