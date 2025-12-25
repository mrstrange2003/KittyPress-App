// compress.cpp (UNIFIED - Archive format only)
#include "compress.h"
#include "kitty.h"
#include "progress.h"
#include "kp_log.h"

#include <zstd.h>
#include <iostream>
#include <vector>
#include <fstream>
#include <filesystem>
#include <cstdint>
#include <stdexcept>
#include <algorithm>
#include <cstring>

using namespace std;
namespace fs = std::__fs::filesystem;

enum KPCodec : uint8_t {
    KP_CODEC_ZSTD = 1
};

static string makeFinalOutputPath(const string &baseOut, const string &storedExt) {
    fs::path p(baseOut);
    if (!storedExt.empty() && p.extension().empty()) {
        p += "." + storedExt;
    }
    return p.string();
}

void storeRawFile(const string &inputPath, const string &outputPath) {
    ifstream in(inputPath, ios::binary);
    if (!in) throw runtime_error("Cannot open input file");

    vector<char> buf((istreambuf_iterator<char>(in)), {});
    in.close();

    ofstream out(outputPath, ios::binary);
    if (!out) throw runtime_error("Cannot open output file");

    out.write(KITTY_MAGIC.data(), KITTY_MAGIC.size());
    bool isCompressed = false;
    out.write((char*)&isCompressed, sizeof(isCompressed));

    string ext = fs::path(inputPath).extension().string();
    if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);

    uint64_t extLen = ext.size();
    out.write((char*)&extLen, sizeof(extLen));
    if (extLen) out.write(ext.data(), extLen);

    uint64_t rawSize = buf.size();
    out.write((char*)&rawSize, sizeof(rawSize));
    if (rawSize) out.write(buf.data(), rawSize);
}

void restoreRawFile(ifstream &in, const string &outputPath) {
    uint64_t rawSize;
    in.read((char*)&rawSize, sizeof(rawSize));

    vector<char> buf(rawSize);
    if (rawSize) in.read(buf.data(), rawSize);

    ofstream out(outputPath, ios::binary);
    if (!out) throw runtime_error("Cannot open output file");

    if (rawSize) out.write(buf.data(), rawSize);
}

void compressFile(const string &inputPath, const string &outputPath) {
    if (!fs::exists(inputPath)) throw runtime_error("Input not found");

    ifstream in(inputPath, ios::binary);
    ofstream out(outputPath, ios::binary | ios::trunc);
    if (!in || !out) throw runtime_error("File open failed");

    string ext = fs::path(inputPath).extension().string();
    if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);

    uint64_t origSize = fs::file_size(inputPath);

    out.write(KITTY_MAGIC.data(), KITTY_MAGIC.size());
    bool isCompressed = true;
    out.write((char*)&isCompressed, sizeof(isCompressed));

    uint64_t extLen = ext.size();
    out.write((char*)&extLen, sizeof(extLen));
    if (extLen) out.write(ext.data(), extLen);

    uint8_t codec = KP_CODEC_ZSTD;
    out.write((char*)&codec, sizeof(codec));

    out.write((char*)&origSize, sizeof(origSize));

    streampos compSizePos = out.tellp();
    uint64_t compSize = 0;
    out.write((char*)&compSize, sizeof(compSize));
    streampos compStart = out.tellp();

    ZSTD_CStream* cs = ZSTD_createCStream();
    ZSTD_initCStream(cs, 1);

    const size_t CHUNK = 64 * 1024;
    vector<char> inBuf(CHUNK), outBuf(CHUNK);

    while (in.good()) {
        in.read(inBuf.data(), CHUNK);
        size_t got = in.gcount();
        if (!got) break;

        ZSTD_inBuffer zin{ inBuf.data(), got, 0 };
        while (zin.pos < zin.size) {
            ZSTD_outBuffer zout{ outBuf.data(), outBuf.size(), 0 };
            ZSTD_compressStream(cs, &zout, &zin);
            out.write(outBuf.data(), zout.pos);
        }
    }

    ZSTD_outBuffer zout{ outBuf.data(), outBuf.size(), 0 };
    ZSTD_endStream(cs, &zout);
    out.write(outBuf.data(), zout.pos);
    ZSTD_freeCStream(cs);

    streampos end = out.tellp();
    compSize = (uint64_t)(end - compStart);
    out.seekp(compSizePos);
    out.write((char*)&compSize, sizeof(compSize));
}

// Stream-to-stream: used by archive to compress individual files
void compressStreamToStream(istream &in, ostream &out, uint64_t origSize,
                            const string &storedExt, uint64_t &outDataSize) {
    outDataSize = 0;
    streampos payloadStart = out.tellp();

    // Write KP05 header for this entry
    out.write(KITTY_MAGIC.data(), KITTY_MAGIC.size());

    uint8_t isCompressed = 1;
    out.write(reinterpret_cast<char*>(&isCompressed), sizeof(uint8_t));

    uint64_t extLen = storedExt.size();
    out.write(reinterpret_cast<char*>(&extLen), sizeof(uint64_t));
    if (extLen) out.write(storedExt.data(), extLen);

    uint8_t codec = KP_CODEC_ZSTD;
    out.write(reinterpret_cast<char*>(&codec), sizeof(uint8_t));

    out.write(reinterpret_cast<char*>(&origSize), sizeof(uint64_t));

    streampos compSizePos = out.tellp();
    uint64_t compSize = 0;
    out.write(reinterpret_cast<char*>(&compSize), sizeof(uint64_t));
    streampos compStart = out.tellp();

    ZSTD_CStream* cs = ZSTD_createCStream();
    ZSTD_initCStream(cs, -3);

    const size_t CHUNK = 256 * 1024;
    vector<char> inBuf(CHUNK), outBuf(CHUNK);

    uint64_t progressBatch = 0;

    while (in.good()) {
        in.read(inBuf.data(), CHUNK);
        size_t got = in.gcount();
        if (!got) break;

        ZSTD_inBuffer zin{ inBuf.data(), got, 0 };
        while (zin.pos < zin.size) {
            ZSTD_outBuffer zout{ outBuf.data(), outBuf.size(), 0 };
            ZSTD_compressStream(cs, &zout, &zin);
            out.write(outBuf.data(), zout.pos);
        }

        progressBatch += got;
        if (progressBatch >= 1024 * 1024) {
            native_progress_add_processed(progressBatch);
            progressBatch = 0;
        }
    }

    ZSTD_outBuffer zout{ outBuf.data(), outBuf.size(), 0 };
    ZSTD_endStream(cs, &zout);
    out.write(outBuf.data(), zout.pos);
    ZSTD_freeCStream(cs);

    if (progressBatch) native_progress_add_processed(progressBatch);

    streampos end = out.tellp();
    compSize = (uint64_t)(end - compStart);

    out.seekp(compSizePos);
    out.write(reinterpret_cast<char*>(&compSize), sizeof(uint64_t));
    out.seekp(end);

    outDataSize = (uint64_t)(end - payloadStart);
}

void compressToStream(const string &inputPath, ostream &out, uint64_t &outDataSize) {
    outDataSize = 0;

    ifstream in(inputPath, ios::binary);
    if (!in) throw runtime_error("Cannot open input");

    string ext = fs::path(inputPath).extension().string();
    if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);

    uint64_t origSize = fs::file_size(inputPath);

    compressStreamToStream(in, out, origSize, ext, outDataSize);
}

void decompressFromStream(istream &in, uint64_t dataSize, const string &outputPath) {
    string magic(4, '\0');
    in.read(magic.data(), 4);

    if (magic != KITTY_MAGIC) {
        throw runtime_error("Bad KP05 magic");
    }

    uint8_t isCompressed;
    in.read(reinterpret_cast<char*>(&isCompressed), sizeof(uint8_t));

    uint64_t extLen;
    in.read(reinterpret_cast<char*>(&extLen), sizeof(uint64_t));

    if (extLen > 255) {
        throw runtime_error("Invalid extLen: " + std::to_string(extLen));
    }

    string ext(extLen, '\0');
    if (extLen) in.read(&ext[0], extLen);

    if (!isCompressed) {
        restoreRawFile((ifstream&)in, makeFinalOutputPath(outputPath, ext));
        return;
    }

    uint8_t codec;
    in.read(reinterpret_cast<char*>(&codec), sizeof(uint8_t));

    if (codec != KP_CODEC_ZSTD) {
        throw runtime_error("Unsupported codec: " + std::to_string(codec));
    }

    uint64_t origSize;
    in.read(reinterpret_cast<char*>(&origSize), sizeof(uint64_t));

    uint64_t compSize;
    in.read(reinterpret_cast<char*>(&compSize), sizeof(uint64_t));

    if (!in.good()) {
        throw runtime_error("Failed to read KP05 header");
    }

    if (compSize == 0 || compSize > 2000000000ULL) {
        throw runtime_error("Invalid compressed size: " + std::to_string(compSize));
    }

    vector<char> compBuf(compSize);
    in.read(compBuf.data(), compSize);

    if (in.gcount() != (streamsize)compSize) {
        throw runtime_error("Failed to read compressed data");
    }

    ofstream out(makeFinalOutputPath(outputPath, ext), ios::binary);
    if (!out) throw runtime_error("Cannot open output");

    ZSTD_DStream* ds = ZSTD_createDStream();
    ZSTD_initDStream(ds);

    const size_t CHUNK = 256 * 1024;
    vector<char> outBuf(CHUNK);

    ZSTD_inBuffer zin{ compBuf.data(), compBuf.size(), 0 };
    while (zin.pos < zin.size) {
        ZSTD_outBuffer zout{ outBuf.data(), outBuf.size(), 0 };
        ZSTD_decompressStream(ds, &zout, &zin);
        out.write(outBuf.data(), zout.pos);
    }

    ZSTD_freeDStream(ds);
}