# KittyPress 🐾

A powerful, fast, and efficient file compression and archive application for Android devices. KittyPress leverages ZSTD compression to deliver industry-leading compression ratios and blazing-fast performance.

## Features

### 🚀 **Performance**
- **Multithreaded Compression & Decompression**: Utilizes all available CPU cores (capped at 4 threads for optimal performance)
- **ZSTD Algorithm**: State-of-the-art compression with excellent speed-to-ratio tradeoff
- **Streaming Architecture**: Direct memory-efficient stream processing with no intermediate temp files
- **Real-time Progress Tracking**: Live compression/extraction progress with percentage updates

### 📦 **Archive Support**
- **Single File Compression**: Compress individual files with full metadata preservation
- **Multi-File Archives**: Create unified archives from multiple files and folders
- **Smart Extraction**: Automatically detects folder structure and preserves directory hierarchy
- **Extension Preservation**: Stores original file extensions for automatic restoration

### 🎨 **User Interface**
- **Dark/Light Mode**: Theme toggle for comfortable viewing in any lighting condition
- **Intuitive Design**: Simple, clean interface with clear action buttons
- **Persistent State**: Remembers user preferences across sessions
- **Progress Visualization**: Animated progress bars during compression/extraction

### 🔐 **File Management**
- **Multiple Input Support**: Select any combination of files and folders
- **Flexible Output**: Save archives to any accessible document folder
- **Directory Preservation**: Maintains folder structure in multi-file archives
- **Efficient Storage**: Significantly reduces file sizes for storage and transfer

## Installation

1. **Clone the Repository**
   ```bash
   git clone https://github.com/yourusername/kittypress.git
   cd kittypress
   ```

2. **Build Requirements**
   - Android Studio (latest version)
   - Android NDK (for C++ compilation)
   - CMake 3.18+
   - Android SDK API 21+

3. **Build the Project**
   ```bash
   # Using Android Studio:
   # File → Open → Select kittypress directory
   # Build → Make Project
   
   # Or via command line:
   ./gradlew build
   ```

4. **Install on Device**
   ```bash
   ./gradlew installDebug
   ```

## Usage

### Compressing Files

1. **Launch KittyPress** on your Android device
2. **Select Input**:
   - Tap **"Pick Files"** to select individual files
   - Tap **"Pick Folder"** to select an entire directory
   - Mix and match files and folders as needed
3. **Compress**:
   - Tap **"Compress"** button
   - Choose destination folder when prompted
   - Monitor progress on the progress bar
4. **Done!** Archive saved as `filename.kitty`

### Extracting Archives

1. **Select Archive**: Tap **"Pick Archive"** to choose a `.kitty` file
2. **Extract**: 
   - Tap **"Extract Selected"**
   - Choose destination folder
   - Watch extraction progress in real-time
3. **Done!** Files extracted with original structure preserved

### Theme Settings

- Tap the **menu icon** (⋮) in the top right
- Toggle between **"Dark Mode"** and **"Light Mode"**
- Selection persists across app sessions

## Technical Details

### Architecture

```
KittyPress App
├── Kotlin UI Layer (MainActivity, NativeProgress)
├── JNI Bridge (native-lib.cpp)
└── C++ Core
    ├── Archive Management (archive.cpp/h)
    ├── Compression Engine (compress.cpp/h)
    ├── Progress Tracking (progress.cpp/h)
    └── ZSTD Integration (zstd library)
```

### Compression Pipeline

**Single File:**
```
Input File → Stream → ZSTD_CCtx (multithreaded) → KP05 Payload → .kitty Archive
```

**Multiple Files/Folders:**
```
Input Files → Archive Format → Per-file ZSTD_CCtx (multithreaded) → Archive Container → .kitty
```

### Decompression Pipeline

```
.kitty Archive → Read Headers → ZSTD_DCtx (multithreaded) → Output Files → Restore Structure
```

### File Format (KP05)

**Single File Format:**
- Magic: `"KP05"` (4 bytes)
- Compressed Flag: 1 byte
- Extension Length: 8 bytes
- Extension: variable
- Codec ID: 1 byte (1 = ZSTD)
- Original Size: 8 bytes
- Compressed Size: 8 bytes
- Compressed Data: variable

**Archive Format:**
- Magic: `"KP05"` (4 bytes)
- Version: 1 byte
- File Count: 4 bytes
- Entries:
  - Path Length: 2 bytes
  - Relative Path: variable
  - Flags: 1 byte
  - Original Size: 8 bytes
  - Compressed Size: 8 bytes
  - Extension Length: 2 bytes
  - Extension: variable
  - KP05 Payload: variable (per file)

## Development

### Project Structure

```
kittypress/
├── app/
│   ├── src/
│   │   ├── main/
│   │   │   ├── java/com/deepion/kittypress/
│   │   │   │   ├── MainActivity.kt
│   │   │   │   ├── KittyPressNative.kt
│   │   │   │   ├── NativeProgress.kt
│   │   │   │   └── SplashActivity.kt
│   │   │   ├── cpp/
│   │   │   │   ├── native-lib.cpp
│   │   │   │   ├── archive.cpp/h
│   │   │   │   ├── compress.cpp/h
│   │   │   │   ├── progress.cpp/h
│   │   │   │   └── kp_log.h
│   │   │   └── res/
│   ├── CMakeLists.txt
│   └── build.gradle
├── external/
│   └── zstd/
│       └── lib/
└── README.md
```

### Building from Source

```bash
# Clone with submodules
git clone --recurse-submodules https://github.com/yourusername/kittypress.git

# Build
cd kittypress
./gradlew assembleDebug

# Install
./gradlew installDebug
```

### Customization

**Adjust Thread Count** (compress.cpp):
```cpp
static int getCompressionThreads() {
    int threads = std::thread::hardware_concurrency();
    if (threads > 8) threads = 8;  // Change cap here
    return threads;
}
```

**Change Compression Level** (compress.cpp):
```cpp
ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, 6);  // 1-22
```

## Troubleshooting

### App Crashes on Compress
- **Check**: Sufficient free storage space
- **Check**: File permissions (read for input, write for output)
- **Check**: File size not exceeding device memory

### Slow Performance
- **Cause**: Device CPU throttling due to heat
- **Solution**: Close background apps, reduce thread count
- **Cause**: Slow I/O (SD card)
- **Solution**: Use internal storage instead

### Extraction Fails
- **Check**: Archive file not corrupted
- **Check**: Destination folder has write permissions
- **Check**: Sufficient free space for extracted files

### Progress Bar Stuck
- **Normal**: Large files may show delayed progress
- **Check**: Device isn't frozen (background task running normally)
- **Restart**: Force close and reopen app if truly frozen

## Performance Tips

1. **For Maximum Speed**:
   - Compress using internal storage (not SD card)
   - Disable background apps
   - Use Cool Device (not under thermal load)

2. **For Maximum Compression**:
   - Increase compression level (adjust ZSTD_c_compressionLevel to 10-15)
   - Compress similar file types together

3. **For Large Files**:
   - Consider splitting into multiple archives
   - Use "Pick Folder" instead of individual files (more efficient)

## Privacy & Security

- ✅ **No Cloud Upload**: All operations local to device
- ✅ **No Telemetry**: Zero data collection or tracking
- ✅ **No Ads**: Clean, ad-free interface
- ✅ **Open Source**: Full code transparency available on GitHub

---

**KittyPress** - Compress smarter, faster, better. 🐾✨

*Made with ❤️ for Android users worldwide*
