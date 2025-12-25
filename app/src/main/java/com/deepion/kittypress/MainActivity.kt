// MainActivity.kt (FIXED - Single File Streaming)
package com.deepion.kittypress

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.util.Log
import android.view.Menu
import android.view.MenuItem
import android.view.View
import android.widget.Button
import android.widget.ProgressBar
import android.widget.TextView
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.app.AppCompatDelegate
import androidx.appcompat.widget.Toolbar
import androidx.documentfile.provider.DocumentFile
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.coroutines.launch
import java.io.File
import java.io.IOException

class MainActivity : AppCompatActivity() {

    private val TAG = "KittyPress"

    private lateinit var statusTv: TextView
    private lateinit var btnCompress: Button
    private lateinit var btnPickFiles: Button
    private lateinit var btnPickFolder: Button
    private lateinit var btnPickArchive: Button
    private lateinit var btnExtractSelected: Button
    private lateinit var progressBar: ProgressBar
    private lateinit var tvProgressPct: TextView

    private var workingFolderTreeUri: Uri? = null

    private val selectedFileUris = mutableListOf<Uri>()
    private val selectedFolderUris = mutableListOf<Uri>()
    private val selectedInputsOrdered = mutableListOf<Uri>()

    private var pendingArchiveToCopy: File? = null
    private var pendingArchiveName: String? = null

    private var selectedArchiveUri: Uri? = null
    private var pendingArchiveToExtract: Uri? = null

    @Volatile private var isCompressing = false
    @Volatile private var isExtracting = false

    companion object {
        const val IntentFlagsForTree =
            Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION

        const val PREFS_NAME = "kittypress_prefs"
        const val PREF_KEY_THEME = "theme_mode"
    }

    private val pickFilesLauncher: ActivityResultLauncher<Array<String>> =
        registerForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
            if (!uris.isNullOrEmpty()) {
                selectedFileUris.clear()
                selectedFileUris.addAll(uris)
                selectedInputsOrdered.removeAll { it in selectedFileUris }
                selectedInputsOrdered.addAll(uris)
                statusTv.text = "KittyPress status: Selected ${uris.size} file(s)"
            } else {
                statusTv.text = "KittyPress status: No files selected."
            }
        }

    private val pickFolderLauncher: ActivityResultLauncher<Uri?> =
        registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { treeUri ->
            if (treeUri == null) {
                statusTv.text = "KittyPress status: No folder picked."
                return@registerForActivityResult
            }
            try {
                contentResolver.takePersistableUriPermission(treeUri, IntentFlagsForTree)
            } catch (_: Exception) {}

            val pendingArchive = pendingArchiveToCopy
            val pendingName = pendingArchiveName
            if (pendingArchive != null && pendingName != null) {
                lifecycleScope.launch(Dispatchers.IO) {
                    val ok = copyFileToDocumentFolder(pendingArchive, treeUri, pendingName)
                    pendingArchiveToCopy = null
                    pendingArchiveName = null
                    if (pendingArchive.exists()) pendingArchive.delete()

                    withContext(Dispatchers.Main) {
                        sessionResetUI()
                        statusTv.text = if (ok) "KittyPress status: Archive saved: $pendingName"
                        else "KittyPress status: Failed to save archive."
                    }
                }
                return@registerForActivityResult
            }

            val pendingExtract = pendingArchiveToExtract
            if (pendingExtract != null) {
                pendingArchiveToExtract = null
                lifecycleScope.launch(Dispatchers.IO) {
                    try {
                        runExtractToFolderInternal(pendingExtract, treeUri)
                        withContext(Dispatchers.Main) {
                            sessionResetUI()
                            statusTv.text = "KittyPress status: Extracted to chosen folder."
                        }
                    } catch (ex: Exception) {
                        Log.e(TAG, "Error extracting to picked folder", ex)
                        withContext(Dispatchers.Main) {
                            sessionResetUI()
                            statusTv.text = "KittyPress status: Failed to extract: ${ex.message}"
                        }
                    }
                }
                return@registerForActivityResult
            }

            workingFolderTreeUri = treeUri
            selectedFolderUris.clear()
            selectedFolderUris.add(treeUri)
            selectedInputsOrdered.remove(treeUri)
            selectedInputsOrdered.add(treeUri)
            statusTv.text =
                "KittyPress status: Working folder selected: ${
                    DocumentFile.fromTreeUri(this, treeUri)?.name ?: treeUri.lastPathSegment
                }"
        }

    private val pickArchiveLauncher: ActivityResultLauncher<Array<String>> =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri != null) {
                selectedArchiveUri = uri
                val name = uriDisplayName(uri) ?: uri.lastPathSegment ?: "archive.kitty"
                statusTv.text = "KittyPress status: Selected archive: $name"
            } else {
                statusTv.text = "KittyPress status: No archive selected."
            }
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        val prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
        val savedMode = prefs.getInt(PREF_KEY_THEME, AppCompatDelegate.MODE_NIGHT_NO)
        AppCompatDelegate.setDefaultNightMode(savedMode)

        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        val toolbar = findViewById<Toolbar>(R.id.mainToolbar)
        setSupportActionBar(toolbar)
        supportActionBar?.setDisplayShowHomeEnabled(true)
        supportActionBar?.setLogo(R.mipmap.ic_launcher)
        supportActionBar?.setDisplayUseLogoEnabled(true)
        supportActionBar?.title = " KittyPress"

        statusTv = findViewById(R.id.tv_status)
        btnCompress = findViewById(R.id.btn_compress)
        btnPickFiles = findViewById(R.id.btn_pick_files)
        btnPickFolder = findViewById(R.id.btn_pick_folder)
        btnPickArchive = findViewById(R.id.btn_pick_archive)
        btnExtractSelected = findViewById(R.id.btn_extract_selected)

        progressBar = findViewById(R.id.progress_bar)
        tvProgressPct = findViewById(R.id.tv_progress_pct)

        try {
            NativeProgress.setProgressCallback { nativePct ->
                runOnUiThread {
                    progressBar.visibility = View.VISIBLE
                    tvProgressPct.visibility = View.VISIBLE
                    progressBar.progress = nativePct.coerceIn(0, 100)
                    tvProgressPct.text = when {
                        isCompressing -> "Compressing: $nativePct%"
                        isExtracting -> "Extracting: $nativePct%"
                        else -> "$nativePct%"
                    }
                }
            }
        } catch (_: Exception) {}

        btnPickFiles.setOnClickListener { pickFilesLauncher.launch(arrayOf("*/*")) }
        btnPickFolder.setOnClickListener { pickFolderLauncher.launch(null) }

        btnCompress.setOnClickListener {
            if (isCompressing) {
                statusTv.text = "KittyPress status: Already compressing..."
                return@setOnClickListener
            }
            statusTv.text = "KittyPress status: Preparing to compress..."
            lifecycleScope.launch { compressAction() }
        }

        btnPickArchive.setOnClickListener {
            pickArchiveLauncher.launch(arrayOf("application/octet-stream", "*/*"))
        }

        btnExtractSelected.setOnClickListener {
            val archive = selectedArchiveUri
            if (archive == null) {
                statusTv.text = "KittyPress status: No archive selected."
                return@setOnClickListener
            }
            if (isExtracting) {
                statusTv.text = "KittyPress status: Extraction already in progress..."
                return@setOnClickListener
            }
            pendingArchiveToExtract = archive
            statusTv.text = "KittyPress status: Choose folder for extraction"
            pickFolderLauncher.launch(null)
        }
    }

    private fun detectSingleFileMode(): Boolean {
        return selectedFileUris.size == 1 && selectedFolderUris.isEmpty()
    }

    private suspend fun compressAction() {
        withContext(Dispatchers.IO) {
            if (isCompressing) return@withContext
            isCompressing = true
            withContext(Dispatchers.Main) { btnCompress.isEnabled = false }

            try {
                if (selectedFolderUris.isEmpty() && selectedFileUris.isEmpty()) {
                    withContext(Dispatchers.Main) {
                        sessionResetUI()
                        statusTv.text = "KittyPress status: No input selected."
                    }
                    isCompressing = false
                    withContext(Dispatchers.Main) { btnCompress.isEnabled = true }
                    return@withContext
                }

                if (detectSingleFileMode()) {
                    withContext(Dispatchers.Main) {
                        statusTv.text = "KittyPress status: Compressing single file..."
                    }
                    compressActionSingleFile()
                } else {
                    withContext(Dispatchers.Main) {
                        statusTv.text = "KittyPress status: Compressing multiple files..."
                    }
                    compressActionMultiFile()
                }

            } catch (ex: Exception) {
                Log.e(TAG, "compressAction", ex)
                withContext(Dispatchers.Main) {
                    sessionResetUI()
                    statusTv.text = "KittyPress status: Error: ${ex.message}"
                }
            } finally {
                isCompressing = false
                withContext(Dispatchers.Main) { btnCompress.isEnabled = true }
            }
        }
    }

    private suspend fun compressActionSingleFile() {
        withContext(Dispatchers.IO) {
            try {
                val inputUri = selectedFileUris[0]
                val inputName = uriDisplayName(inputUri) ?: "file"

                val baseName = inputName.substringBeforeLast('.')
                val outName = "$baseName.kitty"
                val tmpArchive = File(cacheDir, outName)
                if (tmpArchive.exists()) tmpArchive.delete()

                // Ensure parent directory exists
                tmpArchive.parentFile?.mkdirs()

                withContext(Dispatchers.Main) {
                    statusTv.text = "KittyPress status: Copying file to temp..."
                }

                // Copy input URI to temp file
                val tmpInput = File(cacheDir, "input_${System.currentTimeMillis()}_$inputName")
                contentResolver.openInputStream(inputUri)?.use { inputStream ->
                    tmpInput.outputStream().use { fos ->
                        inputStream.copyTo(fos)
                    }
                } ?: throw IOException("Cannot open input URI")

                withContext(Dispatchers.Main) {
                    statusTv.text = "KittyPress status: Running native compression..."
                }

                // Use the archive compression (handles single file too)
                val rc = KittyPressNative.compressNative(
                    arrayOf(tmpInput.absolutePath),  // Pass as array with one element
                    tmpArchive.absolutePath
                )

                tmpInput.delete()

                if (rc != 0) {
                    withContext(Dispatchers.Main) {
                        sessionResetUI()
                        statusTv.text = "KittyPress status: Compression failed."
                    }
                    tmpArchive.delete()
                    return@withContext
                }

                pendingArchiveToCopy = tmpArchive
                pendingArchiveName = outName
                withContext(Dispatchers.Main) {
                    statusTv.text = "KittyPress status: Choose folder to save archive"
                    pickFolderLauncher.launch(null)
                }

            } catch (ex: Exception) {
                Log.e(TAG, "compressActionSingleFile", ex)
                withContext(Dispatchers.Main) {
                    sessionResetUI()
                    statusTv.text = "KittyPress status: Error: ${ex.message}"
                }
            }
        }
    }

    private suspend fun compressActionMultiFile() {
        withContext(Dispatchers.IO) {
            try {
                val inputsRootTmp = File.createTempFile("inputs_", "", cacheDir)
                val inputsRoot = inputsRootTmp.apply {
                    delete()
                    mkdirs()
                }

                if (inputsRoot.exists()) inputsRoot.deleteRecursively()
                inputsRoot.mkdirs()

                val inputsToPass = mutableListOf<String>()

                for (treeUri in selectedFolderUris) {
                    val rootDoc = DocumentFile.fromTreeUri(this@MainActivity, treeUri)
                    if (rootDoc == null) {
                        withContext(Dispatchers.Main) {
                            sessionResetUI()
                            statusTv.text = "KittyPress: Cannot access selected folder."
                        }
                        return@withContext
                    }

                    val folderName = rootDoc.name ?: "folder"
                    val dest = File(inputsRoot, folderName)
                    dest.mkdirs()
                    copyDocumentTreeWithProgress(rootDoc, dest)
                    inputsToPass.add(dest.absolutePath)
                }

                for (fileUri in selectedFileUris) {
                    val name = uriDisplayName(fileUri) ?: "file"
                    val out = File(inputsRoot, name)
                    copyUriToFileWithProgress(fileUri, out)
                    inputsToPass.add(out.absolutePath)
                }

                val baseName = computeBaseNameForArchive(selectedInputsOrdered.firstOrNull())
                    ?: "archive_${System.currentTimeMillis()}"

                val outName = "$baseName.kitty"
                val tmpArchive = File(cacheDir, outName)
                if (tmpArchive.exists()) tmpArchive.delete()

                // Ensure parent directory exists
                tmpArchive.parentFile?.mkdirs()

                withContext(Dispatchers.Main) {
                    statusTv.text = "KittyPress status: Running native compression..."
                }

                val rc = KittyPressNative.compressNative(
                    inputsToPass.toTypedArray(),
                    tmpArchive.absolutePath
                )

                if (rc != 0) {
                    withContext(Dispatchers.Main) {
                        sessionResetUI()
                        statusTv.text = "KittyPress status: Compression failed."
                    }
                    tmpArchive.delete()
                    inputsRoot.deleteRecursively()
                    return@withContext
                }

                pendingArchiveToCopy = tmpArchive
                pendingArchiveName = outName
                withContext(Dispatchers.Main) {
                    statusTv.text = "KittyPress status: Choose folder to save archive"
                    pickFolderLauncher.launch(null)
                }

                inputsRoot.deleteRecursively()

            } catch (ex: Exception) {
                Log.e(TAG, "compressActionMultiFile", ex)
                withContext(Dispatchers.Main) {
                    sessionResetUI()
                    statusTv.text = "KittyPress status: Error: ${ex.message}"
                }
            }
        }
    }

    private suspend fun runExtractToFolderInternal(archiveUri: Uri, treeUri: Uri) {
        if (isExtracting) throw IllegalStateException("Already extracting")
        isExtracting = true
        try {
            withContext(Dispatchers.Main) {
                statusTv.text = "KittyPress status: Running native decompress..."
            }

            val name = uriDisplayName(archiveUri) ?: "archive.kitty"
            val tmpArchive = File(cacheDir, "in_${System.currentTimeMillis()}_$name")
            if (tmpArchive.exists()) tmpArchive.delete()

            // Copy archive to temp file
            contentResolver.openInputStream(archiveUri)?.use { input ->
                tmpArchive.outputStream().use { output ->
                    val buf = ByteArray(256 * 1024)
                    var read: Int
                    while (input.read(buf).also { read = it } >= 0) {
                        output.write(buf, 0, read)
                    }
                    output.flush()
                }
            } ?: throw IOException("Cannot open archive")

            val outDir = File(cacheDir, "out_${System.currentTimeMillis()}")
            if (outDir.exists()) outDir.deleteRecursively()
            outDir.mkdirs()

            withContext(Dispatchers.Main) {
                statusTv.text = "KittyPress status: Extracting archive..."
            }

            // Always use archive extraction (handles 1 or multiple files)
            val extractedRootName = KittyPressNative.decompressNative(
                tmpArchive.absolutePath,
                outDir.absolutePath
            ) ?: throw IOException("Extraction failed")

            val extractedRoot = File(outDir, extractedRootName)
            val baseFolderName = computeBaseNameForArchive(archiveUri)
                ?: name.substringBeforeLast('.', name)

            val destTree = DocumentFile.fromTreeUri(this@MainActivity, treeUri)
                ?: throw IOException("Destination not accessible")

            val destFolder = destTree.findFile(baseFolderName)
                ?: destTree.createDirectory(baseFolderName)
                ?: throw IOException("Cannot create output folder")

            copyDirIntoDocumentFolder(extractedRoot, destFolder)

            tmpArchive.delete()
            outDir.deleteRecursively()

        } finally {
            isExtracting = false
        }
    }

    private fun copyStreamWithProgress(ins: java.io.InputStream, out: java.io.OutputStream) {
        val buf = ByteArray(256 * 1024)
        var read: Int
        try {
            while (ins.read(buf).also { read = it } >= 0) {
                out.write(buf, 0, read)
            }
            out.flush()
        } finally {
            try { ins.close() } catch (_: Exception) {}
            try { out.close() } catch (_: Exception) {}
        }
    }

    private fun copyUriToFileWithProgress(uri: Uri, out: File) {
        contentResolver.openInputStream(uri)?.use { ins ->
            out.outputStream().use { fos ->
                copyStreamWithProgress(ins, fos)
            }
        } ?: throw IOException("Cannot open input URI")
    }

    private fun copyDocumentTreeWithProgress(doc: DocumentFile, dest: File) {
        if (doc.isDirectory) {
            dest.mkdirs()
            doc.listFiles().forEach { child ->
                val childName = child.name ?: "unknown"
                val childDest = File(dest, childName)
                if (child.isDirectory) {
                    childDest.mkdirs()
                    copyDocumentTreeWithProgress(child, childDest)
                } else {
                    contentResolver.openInputStream(child.uri)?.use { input ->
                        childDest.outputStream().use { out ->
                            copyStreamWithProgress(input, out)
                        }
                    }
                }
            }
        } else {
            val name = doc.name ?: "file"
            val outFile = File(dest, name)
            contentResolver.openInputStream(doc.uri)?.use { input ->
                outFile.outputStream().use { out ->
                    copyStreamWithProgress(input, out)
                }
            }
        }
    }

    private fun copyDirIntoDocumentFolder(src: File, destDir: DocumentFile) {
        if (src.isFile) {
            val name = src.name
            destDir.findFile(name)?.delete()
            val created = destDir.createFile("application/octet-stream", name)
                ?: throw IOException("Failed to create file")

            contentResolver.openOutputStream(created.uri)?.use { out ->
                src.inputStream().use { it.copyTo(out) }
            }
            return
        }

        src.listFiles()?.forEach { f ->
            if (f.isDirectory) {
                val sub = destDir.findFile(f.name) ?: destDir.createDirectory(f.name)
                ?: throw IOException("Failed to create dir ${f.name}")
                copyDirIntoDocumentFolder(f, sub)
            } else {
                val file = destDir.findFile(f.name) ?: destDir.createFile(
                    "application/octet-stream",
                    f.name
                ) ?: throw IOException("Failed to create file ${f.name}")

                contentResolver.openOutputStream(file.uri)?.use { out ->
                    f.inputStream().use { it.copyTo(out) }
                }
            }
        }
    }

    private fun uriDisplayName(uri: Uri): String? {
        try {
            val doc = DocumentFile.fromSingleUri(this, uri)
            if (doc?.name != null) return doc.name
        } catch (_: Exception) {}
        return uri.lastPathSegment
    }

    private fun copyFileToDocumentFolder(src: File, folder: Uri, name: String): Boolean {
        return try {
            val root = DocumentFile.fromTreeUri(this, folder) ?: return false
            root.findFile(name)?.delete()
            val created = root.createFile("application/octet-stream", name) ?: return false

            contentResolver.openOutputStream(created.uri)?.use { out ->
                src.inputStream().use { it.copyTo(out) }
            }
            true
        } catch (e: Exception) {
            Log.e(TAG, "copyFileToDocumentFolder", e)
            false
        }
    }

    private fun computeBaseNameForArchive(uri: Uri?): String? {
        uri ?: return null
        try {
            val d = DocumentFile.fromSingleUri(this, uri)
            if (d?.name != null) {
                val name = d.name!!
                // Remove extension if present and return just the name (no path separators)
                return name.substringBeforeLast('.', name)
            }
        } catch (_: Exception) {}

        // Fallback: try to extract from lastPathSegment
        val segment = uri.lastPathSegment
        if (segment != null) {
            // Remove "primary:" prefix if present and everything after the last /
            var cleaned = if (segment.contains(':')) {
                segment.substringAfter(':')
            } else {
                segment
            }
            // Get just the last part (folder name) if there are path separators
            cleaned = cleaned.substringAfterLast('/')
            return cleaned.substringBeforeLast('.')
        }
        return null
    }

    private fun sessionResetUI(preserveStatus: Boolean = true) {
        val lastStatus = if (preserveStatus) statusTv.text else "KittyPress status: Ready"

        selectedFileUris.clear()
        selectedFolderUris.clear()
        selectedInputsOrdered.clear()
        pendingArchiveToCopy = null
        pendingArchiveName = null
        selectedArchiveUri = null
        pendingArchiveToExtract = null
        workingFolderTreeUri = null

        isCompressing = false
        isExtracting = false

        progressBar.visibility = View.GONE
        tvProgressPct.visibility = View.GONE
        progressBar.progress = 0
        tvProgressPct.text = ""

        statusTv.text = lastStatus

        btnCompress.isEnabled = true
    }

    override fun onCreateOptionsMenu(menu: Menu?): Boolean {
        menuInflater.inflate(R.menu.menu_main, menu)
        updateDarkModeMenuTitle(menu)
        return true
    }

    override fun onOptionsItemSelected(item: MenuItem): Boolean {
        return when (item.itemId) {
            R.id.menu_dark_mode -> {
                toggleDarkMode()
                // Update menu title after toggling
                invalidateOptionsMenu()
                true
            }
            R.id.menu_exit -> {
                exitApp()
                true
            }
            else -> super.onOptionsItemSelected(item)
        }
    }

    private fun updateDarkModeMenuTitle(menu: Menu?) {
        val darkModeItem = menu?.findItem(R.id.menu_dark_mode)
        val prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
        val currentMode = prefs.getInt(PREF_KEY_THEME, AppCompatDelegate.MODE_NIGHT_NO)

        val title = if (currentMode == AppCompatDelegate.MODE_NIGHT_YES) {
            "Light Mode"
        } else {
            "Dark Mode"
        }
        darkModeItem?.title = title
    }

    private fun toggleDarkMode() {
        val prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
        val currentMode = prefs.getInt(PREF_KEY_THEME, AppCompatDelegate.MODE_NIGHT_NO)

        val newMode = if (currentMode == AppCompatDelegate.MODE_NIGHT_NO) {
            AppCompatDelegate.MODE_NIGHT_YES
        } else {
            AppCompatDelegate.MODE_NIGHT_NO
        }

        prefs.edit().putInt(PREF_KEY_THEME, newMode).apply()
        AppCompatDelegate.setDefaultNightMode(newMode)

        // Optional: show status
        val modeStr = if (newMode == AppCompatDelegate.MODE_NIGHT_YES) "Dark" else "Light"
        statusTv.text = "KittyPress status: Switched to $modeStr mode"
    }

    private fun exitApp() {
        finishAffinity()
    }
}