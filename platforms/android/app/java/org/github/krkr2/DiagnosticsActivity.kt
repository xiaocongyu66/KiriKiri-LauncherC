package org.github.krkr2

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.widget.Toast
import androidx.activity.compose.setContent
import androidx.appcompat.app.AppCompatActivity
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Share
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import java.io.File
import java.io.RandomAccessFile

class DiagnosticsActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            LauncherTheme {
                DiagnosticsScreen(
                    onBack = { finish() },
                )
            }
        }
    }
}

private data class DiagSnapshot(
    val versionName: String,
    val versionCode: Long,
    val packageName: String,
    val abi: String,
    val nativeBuildId: String,
    val nativeFatalLog: String,
    val nativeFatalPath: String,
    val engineLog: String,
    val engineLogPath: String,
)

private fun loadSnapshot(context: Context): DiagSnapshot {
    val pkg = context.packageName
    val pm = context.packageManager
    val info = runCatching { pm.getPackageInfo(pkg, 0) }.getOrNull()
    val verName = info?.versionName ?: "unknown"
    val verCode = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
        info?.longVersionCode ?: -1L
    } else {
        @Suppress("DEPRECATION")
        info?.versionCode?.toLong() ?: -1L
    }
    val abi = Build.SUPPORTED_ABIS.firstOrNull() ?: "unknown"

    // Native BuildID: parse the .note.gnu.build-id note from libkrkr2.so. We
    // walk the program headers via /proc/self/maps to find the file path,
    // then read the GNU build-id note. Falls back to "unknown" if anything
    // goes wrong; this is best-effort diagnostic info, not a hard dependency.
    val buildId = readNativeBuildId(context)

    val logDir = File(LauncherPrefs.getLogDir(context))

    val fatalCandidates = listOf(
        File(logDir, "krkr2_native_fatal.log"),
        File("/storage/emulated/0/Android/data/$pkg/files/krkr2_native_fatal.log"),
    )
    val fatalFile = fatalCandidates.firstOrNull { it.exists() }
    val nativeFatalLog = fatalFile?.takeIf { it.length() > 0 }?.let {
        // Cap at last 64 KB so we never OOM the UI.
        tailFile(it, lines = Int.MAX_VALUE, maxBytes = 64 * 1024)
    } ?: ""

    val engineLogFile = LauncherPrefs.latestUnifiedLogFile(context)
        ?: File(LauncherPrefs.getLogDir(context), "yyyyMMddHHmm.log")
    val engineLog = if (engineLogFile.exists() && engineLogFile.length() > 0) {
        tailFile(engineLogFile, lines = Int.MAX_VALUE, maxBytes = 64 * 1024)
    } else ""

    return DiagSnapshot(
        versionName = verName,
        versionCode = verCode,
        packageName = pkg,
        abi = abi,
        nativeBuildId = buildId,
        nativeFatalLog = nativeFatalLog,
        nativeFatalPath = (fatalFile ?: fatalCandidates.first()).absolutePath,
        engineLog = engineLog,
        engineLogPath = engineLogFile.absolutePath,
    )
}

private fun readNativeBuildId(context: Context): String {
    return runCatching {
        val nativeDir = context.applicationInfo.nativeLibraryDir
        val so = File(nativeDir, "libkrkr2.so")
        if (!so.exists()) return@runCatching "(libkrkr2.so missing)"
        // ELF parsing: read .note.gnu.build-id by walking section headers.
        // Quick path: find the magic bytes "GNU\0" near the start of the
        // file, the 16-byte build-id follows after a 12-byte note header.
        RandomAccessFile(so, "r").use { raf ->
            val cap = minOf(raf.length(), 64 * 1024L).toInt()
            val buf = ByteArray(cap)
            raf.readFully(buf)
            val needle = byteArrayOf('G'.code.toByte(), 'N'.code.toByte(), 'U'.code.toByte(), 0)
            var idx = -1
            for (i in 0..(buf.size - needle.size - 20)) {
                if (buf[i] == needle[0] && buf[i + 1] == needle[1] &&
                    buf[i + 2] == needle[2] && buf[i + 3] == needle[3]
                ) { idx = i; break }
            }
            if (idx < 0) return@runCatching "(no build-id note)"
            val start = idx + 4
            val end = minOf(start + 20, buf.size)
            val sb = StringBuilder(40)
            for (i in start until end) sb.append("%02x".format(buf[i].toInt() and 0xff))
            sb.toString()
        }
    }.getOrElse { "(read failed: ${it.javaClass.simpleName})" }
}

private fun tailFile(file: File, lines: Int, maxBytes: Int = 256 * 1024): String {
    if (!file.exists()) return ""
    val len = file.length()
    val readLen = minOf(len, maxBytes.toLong()).toInt()
    val bytes = ByteArray(readLen)
    RandomAccessFile(file, "r").use { raf ->
        raf.seek(len - readLen)
        raf.readFully(bytes)
    }
    val all = String(bytes, Charsets.UTF_8)
    if (lines == Int.MAX_VALUE) return all
    val split = all.split('\n')
    val take = if (split.size > lines) split.subList(split.size - lines, split.size) else split
    return take.joinToString("\n")
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun DiagnosticsScreen(onBack: () -> Unit) {
    val context = LocalContext.current
    val text = LauncherStrings.current(context)
    var snap by remember { mutableStateOf(loadSnapshot(context)) }

    Scaffold(
        topBar = {
            TopAppBar(
                navigationIcon = {
                    IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, null) }
                },
                title = { Text(text.diagnosticsTitle) },
                actions = {
                    IconButton(onClick = { snap = loadSnapshot(context) }) {
                        Icon(Icons.Default.Refresh, null)
                    }
                    IconButton(onClick = {
                        val combined = buildShareText(snap, text)
                        copyToClipboard(context, combined, text.copied)
                    }) { Icon(Icons.Default.ContentCopy, null) }
                    IconButton(onClick = {
                        shareText(context, buildShareText(snap, text))
                    }) { Icon(Icons.Default.Share, null) }
                    IconButton(onClick = {
                        LauncherPrefs.clearUnifiedLogs(context)
                        runCatching { File(snap.nativeFatalPath).delete() }
                        snap = loadSnapshot(context)
                        toast(context, text.cleared)
                    }) { Icon(Icons.Default.Delete, null) }
                },
                colors = TopAppBarDefaults.topAppBarColors(containerColor = Color(0xFF101014))
            )
        }
    ) { padding ->
        Surface(Modifier.fillMaxSize().padding(padding), color = Color(0xFF0C0C10)) {
            Column(
                Modifier.fillMaxSize().padding(16.dp).verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                InfoCard(text.appVersion, "${snap.versionName} (#${snap.versionCode})\n${snap.packageName}")
                InfoCard(text.appAbi, snap.abi)
                InfoCard(text.appBuildId, snap.nativeBuildId)

                LogCard(
                    title = text.nativeFatalLog,
                    body = snap.nativeFatalLog.ifBlank { text.noFatalLog },
                    pathHint = snap.nativeFatalPath,
                )

                LogCard(
                    title = text.engineLog,
                    body = snap.engineLog.ifBlank { text.noEngineLog },
                    pathHint = snap.engineLogPath,
                )

                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    FilledTonalButton(onClick = { snap = loadSnapshot(context) }) { Text(text.refresh) }
                    FilledTonalButton(onClick = onBack) { Text(text.close) }
                }
            }
        }
    }
}

@Composable
private fun InfoCard(title: String, value: String) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(12.dp),
        color = Color(0xFF16161B),
    ) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(title, color = Color(0xFFAAAAAA), style = MaterialTheme.typography.labelMedium)
            Text(value, color = Color.White, style = MaterialTheme.typography.bodyMedium, fontFamily = FontFamily.Monospace)
        }
    }
}

@Composable
private fun LogCard(title: String, body: String, pathHint: String) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(12.dp),
        color = Color(0xFF14141A),
    ) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Text(title, color = Color(0xFFCCCCCC), style = MaterialTheme.typography.labelMedium)
            Text(pathHint, color = Color(0xFF707080), style = MaterialTheme.typography.labelSmall, fontFamily = FontFamily.Monospace)
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .background(Color(0xFF09090E), shape = RoundedCornerShape(8.dp))
                    .padding(8.dp),
            ) {
                Text(
                    body,
                    color = Color(0xFFEEEEEE),
                    fontSize = 11.sp,
                    fontFamily = FontFamily.Monospace,
                )
            }
        }
    }
}

private fun buildShareText(snap: DiagSnapshot, text: LauncherStrings.Texts): String = buildString {
    append("===== ").append(text.diagnosticsTitle).append(" =====\n")
    append(text.appVersion).append(": ").append(snap.versionName).append(" (#").append(snap.versionCode).append(")\n")
    append("packageName: ").append(snap.packageName).append('\n')
    append(text.appAbi).append(": ").append(snap.abi).append('\n')
    append(text.appBuildId).append(": ").append(snap.nativeBuildId).append('\n')
    append('\n').append("--- ").append(text.nativeFatalLog).append(" ---\n")
    append(snap.nativeFatalLog.ifBlank { text.noFatalLog }).append('\n')
    append('\n').append("--- ").append(text.engineLog).append(" ---\n")
    append(snap.engineLog.ifBlank { text.noEngineLog })
}

private fun copyToClipboard(context: Context, value: String, toastMsg: String) {
    val cm = context.getSystemService(Context.CLIPBOARD_SERVICE) as? ClipboardManager
    cm?.setPrimaryClip(ClipData.newPlainText("krkr2-diagnostics", value))
    toast(context, toastMsg)
}

private fun shareText(context: Context, value: String) {
    val intent = Intent(Intent.ACTION_SEND).apply {
        type = "text/plain"
        putExtra(Intent.EXTRA_TEXT, value)
    }
    context.startActivity(Intent.createChooser(intent, "Share").addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
}

private fun toast(context: Context, message: String) {
    Toast.makeText(context, message, Toast.LENGTH_SHORT).show()
}
