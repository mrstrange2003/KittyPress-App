// compress.h
#pragma once
#include <string>
#include <fstream>
#include <vector>
#include <iosfwd>
#include <cstdint>

// High-level API used by other parts of the app:
//
// Existing file-based KP05 compressor/decompressor (still available if you need):
// compressFile: reads inputPath (file on disk) and writes a KP05-wrapped compressed file at outputPath.
// decompressFile: reads a KP05-wrapped file from inputPath and writes the original file at outputPath.
void compressFile(const std::string &inputPath, const std::string &outputPath);
void decompressFile(const std::string &inputPath, const std::string &outputPath);

// NEW: Stream-to-stream compression (no intermediate storage)
// compressStreamToStream: reads from 'in' stream, compresses with zstd, writes KP05 payload to 'out' stream
//                         origSize: original file size (for progress tracking)
//                         storedExt: file extension to store in KP05 header (without leading dot)
//                         outDataSize: receives total bytes written to output (size of complete KP05 payload)
void compressStreamToStream(std::istream &in, std::ostream &out, uint64_t origSize,
                            const std::string &storedExt, uint64_t &outDataSize);

// Streaming KP05 helpers (used by archive to avoid temp buffers):
// compressToStream: reads inputPath and writes a KP05-wrapped compressed payload directly into 'out'.
//                  outDataSize receives the number of bytes written (size of KP05 payload).
void compressToStream(const std::string &inputPath, std::ostream &out, uint64_t &outDataSize);

// decompressFromStream: reads a KP05-wrapped payload from 'in' (starting at current position)
//                       and writes the original file to outputPath.
//                       dataSize is provided for context by the caller, but the function
//                       reads exactly what KP05 format dictates (including compSize).
void decompressFromStream(std::istream &in, uint64_t dataSize, const std::string &outputPath);

// Raw store/restore helpers (used when storing an uncompressed payload inside a KP05 file)
void storeRawFile(const std::string &inputPath, const std::string &outputPath);
void restoreRawFile(std::ifstream &inStream, const std::string &outputPath);