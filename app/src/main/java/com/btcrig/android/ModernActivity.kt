package com.btcrig.android

import android.Manifest
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.PowerManager
import android.provider.Settings
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.enableEdgeToEdge
import androidx.activity.compose.setContent
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.Crossfade
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.slideInVertically
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.Alignment
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.delay
import java.io.File
import java.io.FileInputStream
import java.net.HttpURLConnection
import java.net.URL
import java.util.Locale
import java.util.UUID
import org.json.JSONArray
import org.json.JSONObject

class ModernActivity : ComponentActivity() {
    private var serviceState = ""
    private var refreshUi: (() -> Unit)? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        requestNotificationPermission()

        setContent {
            var ui by remember { mutableStateOf(readUi()) }
            var page by remember { mutableStateOf(0) }
            var showJson by remember { mutableStateOf(false) }
            var showLog by remember { mutableStateOf(false) }
            var logText by remember { mutableStateOf("") }
            var basic by remember { mutableStateOf(readBasic()) }
            var benchmark by remember { mutableStateOf(loadBenchmarkText()) }
            var benchmarking by remember { mutableStateOf(false) }
            var rankMode by remember { mutableStateOf("all") }
            var leaderboard by remember { mutableStateOf(defaultLeaderboard()) }
            var update by remember { mutableStateOf(UpdateState()) }

            fun refreshUpdate() {
                checkForUpdates(ui.version) { update = it }
            }
            fun refreshLeaderboard() {
                fetchLeaderboard(rankMode) { leaderboard = it }
            }

            DisposableEffect(Unit) {
                refreshUi = {
                    serviceState = ""
                    basic = readBasic()
                    ui = readUi()
                    refreshUpdate()
                }
                onDispose { refreshUi = null }
            }
            fun saveBasic(next: BtcrigConfig.Basic) {
                basic = next
                runCatching { BtcrigConfig.writeBasic(this, next) }
                    .onSuccess { ui = readUi() }
                    .onFailure { error -> toast(getString(R.string.save_failed, error.message)) }
            }
            fun refreshSoon() {
                Thread {
                    Thread.sleep(700)
                    runOnUiThread { ui = readUi() }
                }.start()
            }

            LaunchedEffect(ui.running) {
                while (ui.running) {
                    delay(2000)
                    ui = readUi()
                }
            }

            LaunchedEffect(Unit) {
                refreshUpdate()
            }

            LaunchedEffect(rankMode) {
                refreshLeaderboard()
            }

            BtcrigTheme {
                BtcrigScreen(
                    ui = ui,
                    update = update,
                    page = page,
                    benchmark = benchmark,
                    rankMode = rankMode,
                    leaderboard = leaderboard,
                    benchmarking = benchmarking,
                    onPage = { page = it },
                    onRankMode = { rankMode = it },
                    onOpenUpdate = { openRelease(update) },
                    onStart = {
                        if (startBtcrigService()) {
                            setServiceExpectedRunning(true)
                            serviceState = "running"
                            ui = readUi().copy(running = true, service = "running")
                            refreshSoon()
                        } else {
                            page = 1
                            ui = readUi()
                        }
                    },
                    onStop = {
                        setServiceExpectedRunning(false)
                        stopBtcrigService()
                        serviceState = "stopped"
                        ui = readUi().copy(running = false, service = "stopped")
                        refreshSoon()
                    },
                    onBenchmark = {
                        if (ui.running) {
                            toast(getString(R.string.stop_mining_before_benchmark))
                            return@BtcrigScreen
                        }
                        benchmarking = true
                        val configPath = runCatching { BtcrigConfig.ensure(this).absolutePath }.getOrDefault("")
                        val threads = Runtime.getRuntime().availableProcessors().coerceAtLeast(1)
                        val benchmarkTitle = getString(R.string.benchmark)
                        val benchmarkDuration = getString(R.string.benchmark_duration_value, BENCHMARK_SECONDS)
                        val cpuFullCores = getString(R.string.cpu_full_cores_value, threads.toString())
                        val testing = getString(R.string.testing)
                        val unavailable = getString(R.string.unavailable)
                        Thread {
                            val lines = mutableListOf(benchmarkTitle, benchmarkDuration, cpuFullCores, "")
                            fun showTesting(label: String) {
                                runOnUiThread {
                                    benchmark = (lines + getString(R.string.benchmark_backend_value, label, testing)).joinToString("\n")
                                }
                            }
                            showTesting("CPU")
                            val cpuHps = BtcrigNative.benchmarkCpu(BENCHMARK_SECONDS, threads)
                            lines.add(getString(R.string.benchmark_backend_value, "CPU", if (cpuHps >= 0.0) formatHashrate(cpuHps) else unavailable))
                            showTesting("GPU")
                            val gpuHps = BtcrigNative.benchmarkOpencl(configPath, BENCHMARK_SECONDS)
                            lines.add(getString(R.string.benchmark_backend_value, "GPU", if (gpuHps >= 0.0) formatHashrate(gpuHps) else unavailable))
                            showTesting("CPU + GPU")
                            val cpuGpuHps = if (gpuHps >= 0.0) BtcrigNative.benchmarkCpuGpu(configPath, BENCHMARK_SECONDS, threads) else -1.0
                            lines.add(getString(R.string.benchmark_backend_value, "CPU + GPU", if (cpuGpuHps >= 0.0) formatHashrate(cpuGpuHps) else unavailable))
                            val submitText = runCatching { submitBenchmark(cpuHps, gpuHps, cpuGpuHps) }
                                .getOrElse { error -> getString(R.string.leaderboard_upload_failed, error.message ?: error.javaClass.simpleName) }
                            lines.add("")
                            lines.add(submitText)
                            val result = lines.joinToString("\n")
                            saveBenchmarkText(result)
                            runOnUiThread {
                                benchmarking = false
                                benchmark = result
                                refreshLeaderboard()
                            }
                        }.start()
                    },
                    basic = basic,
                    onBasicChange = { saveBasic(it) },
                    onBatteryOptimization = { requestIgnoreBatteryOptimizations() },
                    onJson = { showJson = true },
                    onLog = {
                        logText = readLogText()
                        showLog = true
                    },
                )

                if (showJson) {
                    JsonDialog(
                        text = runCatching { BtcrigConfig.read(this) }.getOrDefault("{}"),
                        onDismiss = { showJson = false },
                        onSave = {
                            runCatching { BtcrigConfig.write(this, it) }
                                .onSuccess {
                                    showJson = false
                                    basic = readBasic()
                                    ui = readUi()
                                    toast(getString(R.string.config_saved))
                                }
                                .onFailure { error -> toast(getString(R.string.save_failed, error.message)) }
                        },
                    )
                }

                if (showLog) {
                    TextDialog(
                        title = "btcrig.log",
                        text = logText,
                        onCopy = { copyToClipboard("btcrig.log", logText) },
                        onDismiss = { showLog = false },
                    )
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        refreshUi?.invoke()
    }

    private fun requestNotificationPermission() {
        if (Build.VERSION.SDK_INT >= 33 &&
            checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
        ) {
            requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 1)
        }
    }

    private fun startBtcrigService(): Boolean {
        val basic = runCatching { BtcrigConfig.readBasic(this) }.getOrNull() ?: return false
        if (basic.poolUrl.trim().isEmpty() || basic.user.trim().isEmpty()) {
            toast(getString(R.string.configure_pool_user_first))
            return false
        }
        if (basic.cpuThreads <= 0 && !basic.openclEnabled) {
            toast(getString(R.string.enable_cpu_or_opencl_first))
            return false
        }
        runCatching { BtcrigConfig.writeBasic(this, basic) }
            .onFailure { error ->
                toast(getString(R.string.config_save_failed, error.message))
                return false
            }
        val intent = Intent(this, BtcrigService::class.java)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) startForegroundService(intent) else startService(intent)
        return true
    }

    private fun stopBtcrigService() {
        runCatching {
            startService(Intent(this, BtcrigService::class.java).setAction(BtcrigService.ACTION_STOP))
        }.onFailure { error ->
            Thread {
                runCatching { BtcrigNative.stop() }
                stopService(Intent(this, BtcrigService::class.java))
            }.start()
            toast(getString(R.string.stop_failed, error.message ?: error.javaClass.simpleName))
        }
    }

    private fun readUi(): UiState {
        val running = BtcrigNative.isRunning()
        val expectedRunning = serviceExpectedRunning()
        val configPath = runCatching { BtcrigConfig.ensure(this).absolutePath }.getOrDefault(getString(R.string.unavailable_wrapped))
        val logFile = File(filesDir, "btcrig.log")
        val logPath = logFile.absolutePath
        val configuredPool = runCatching { BtcrigConfig.readBasic(this).poolUrl.ifBlank { getString(R.string.not_configured) } }
            .getOrDefault(getString(R.string.unavailable))
        val configSummary = runCatching {
            val basic = BtcrigConfig.readBasic(this)
            val cpu = if (basic.cpuThreads > 0) getString(R.string.cpu_threads_value, basic.cpuThreads) else getString(R.string.disabled)
            val openclValue = if (basic.openclEnabled) getString(R.string.enabled) else getString(R.string.disabled)
            getString(R.string.cpu_opencl_summary, cpu, openclValue)
        }.getOrDefault(getString(R.string.config_summary_unavailable))
        val opencl = runCatching { BtcrigNative.openclStatus(configPath) }
            .getOrDefault("Config: unavailable\nRuntime: not probed\nMode: CPU only")
        val nativeError = cleanLog(BtcrigNative.lastError()).trim()
        val logError = recentLogError(logFile)
        val error = readableError(if (nativeError.startsWith("core returned")) logError.ifBlank { nativeError } else nativeError.ifBlank { logError })
            .ifBlank { if (!running && expectedRunning) getString(R.string.service_not_running_hint) else "" }

        return UiState(
            version = versionName(),
            backend = BtcrigNative.backendName(),
            selfTest = BtcrigNative.selfTest(),
            running = running,
            service = serviceState.ifEmpty { if (running) "running" else if (expectedRunning) "missing" else "stopped" },
            hashrate = if (running) formatHashrate(BtcrigNative.hashrate()) else "-- H/s",
            workers = if (running) getString(R.string.workers_value, BtcrigNative.workerCount()) else getString(R.string.workers_empty),
            total = if (running) getString(R.string.total_value, BtcrigNative.totalHashes()) else getString(R.string.total_empty),
            pool = if (running) BtcrigNative.pool().ifBlank { getString(R.string.not_configured_wrapped) } else configuredPool,
            stratum = if (running) {
                getString(
                    R.string.stratum_running,
                    BtcrigNative.stratumStatus(),
                    if (BtcrigNative.stratumConnected()) getString(R.string.yes) else getString(R.string.no),
                    BtcrigNative.stratumJobs(),
                )
            } else {
                getString(R.string.stratum_stopped)
            },
            shares = if (running) {
                getString(R.string.shares_running, BtcrigNative.stratumSubmits(), BtcrigNative.stratumAccepts(), BtcrigNative.stratumRejects())
            } else {
                getString(R.string.shares_empty)
            },
            error = error,
            opencl = opencl,
            cpuSummary = cpuSummary(),
            gpuSummary = gpuSummary(opencl),
            configSummary = configSummary,
            configPath = configPath,
            logPath = logPath,
        )
    }

    private fun toast(text: String) = Toast.makeText(this, text, Toast.LENGTH_SHORT).show()

    private fun readBasic(): BtcrigConfig.Basic =
        runCatching { BtcrigConfig.readBasic(this) }.getOrElse { BtcrigConfig.Basic() }

    private fun readLogText(): String = readTail(
        File(filesDir, "btcrig.log"),
        64 * 1024,
        getString(R.string.log_not_found),
        getString(R.string.empty_log),
    ) { getString(R.string.log_read_failed, it) }

    private fun readableError(raw: String): String {
        val text = cleanLog(raw).trim()
        if (text.isEmpty()) return ""
        val lower = text.lowercase(Locale.US)
        val hint = when {
            "sslhandshakeexception" in lower || "certificate" in lower -> R.string.error_hint_tls
            "opencl" in lower && ("failed" in lower || "unavailable" in lower || "not found" in lower) -> R.string.error_hint_opencl
            "connect" in lower || "[net]" in lower || "closed connection" in lower -> R.string.error_hint_pool
            "json" in lower || "bad config" in lower || "invalid url" in lower -> R.string.error_hint_config
            else -> 0
        }
        return if (hint == 0) text else "${getString(hint)}\n$text"
    }

    private fun copyToClipboard(label: String, text: String) {
        val clipboard = getSystemService(CLIPBOARD_SERVICE) as ClipboardManager
        clipboard.setPrimaryClip(ClipData.newPlainText(label, text))
        toast(getString(R.string.copied_to_clipboard))
    }

    private fun loadBenchmarkText(): String =
        getSharedPreferences("benchmark", MODE_PRIVATE).getString("last_text", null) ?: defaultBenchmarkText()

    private fun saveBenchmarkText(text: String) {
        getSharedPreferences("benchmark", MODE_PRIVATE).edit().putString("last_text", text).apply()
    }

    private fun installId(): String {
        val prefs = getSharedPreferences("rank", MODE_PRIVATE)
        return prefs.getString("install_id", null) ?: UUID.randomUUID().toString().also {
            prefs.edit().putString("install_id", it).apply()
        }
    }

    private fun serviceExpectedRunning(): Boolean =
        getSharedPreferences("service", MODE_PRIVATE).getBoolean("desired_running", false)

    private fun setServiceExpectedRunning(running: Boolean) {
        getSharedPreferences("service", MODE_PRIVATE)
            .edit()
            .putBoolean("desired_running", running)
            .apply()
    }

    private fun requestIgnoreBatteryOptimizations() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            toast(getString(R.string.battery_optimization_not_needed))
            return
        }
        val manager = getSystemService(POWER_SERVICE) as? PowerManager
        if (manager?.isIgnoringBatteryOptimizations(packageName) == true) {
            toast(getString(R.string.battery_optimization_already_ignored))
            return
        }
        val request = Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS, Uri.parse("package:$packageName"))
        runCatching { startActivity(request) }.onFailure {
            runCatching { startActivity(Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS)) }
                .onFailure { error -> toast(getString(R.string.battery_optimization_open_failed, error.message ?: error.javaClass.simpleName)) }
        }
    }

    private fun checkForUpdates(currentVersion: String, onResult: (UpdateState) -> Unit) {
        val prefs = getSharedPreferences("updates", MODE_PRIVATE)
        val cachedVersion = prefs.getString("latest_version", "").orEmpty()
        val cachedUrl = prefs.getString("release_url", "").orEmpty()
        if (cachedVersion.isNotBlank()) {
            onResult(updateStateFor(currentVersion, cachedVersion, cachedUrl))
        }
        val now = System.currentTimeMillis()
        if (now - prefs.getLong("checked_at", 0L) < UPDATE_CACHE_MS) {
            return
        }
        onResult(updateStateFor(currentVersion, cachedVersion, cachedUrl, checking = true))
        Thread {
            val result = runCatching { fetchLatestRelease() }
            runOnUiThread {
                result
                    .onSuccess { release ->
                        prefs.edit()
                            .putString("latest_version", release.version)
                            .putString("release_url", release.url)
                            .putLong("checked_at", System.currentTimeMillis())
                            .apply()
                        onResult(updateStateFor(currentVersion, release.version, release.url))
                    }
                    .onFailure { error ->
                        prefs.edit().putLong("checked_at", System.currentTimeMillis()).apply()
                        onResult(updateStateFor(currentVersion, cachedVersion, cachedUrl).copy(error = error.message ?: error.javaClass.simpleName))
                    }
            }
        }.start()
    }

    private fun openRelease(update: UpdateState) {
        val url = update.url.ifBlank { UPDATE_RELEASE_URL }
        runCatching { startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url))) }
    }

    private fun defaultBenchmarkText(): String =
        listOf(
            getString(R.string.benchmark),
            getString(R.string.benchmark_duration_value, BENCHMARK_SECONDS),
            getString(R.string.cpu_full_cores_value, Runtime.getRuntime().availableProcessors().coerceAtLeast(1).toString()),
            "",
            getString(R.string.benchmark_backend_value, "CPU", "--"),
            getString(R.string.benchmark_backend_value, "GPU", "--"),
            getString(R.string.benchmark_backend_value, "CPU + GPU", "--"),
        ).joinToString("\n")

    private fun defaultLeaderboard(): RankUi =
        RankUi(getString(R.string.leaderboard), getString(R.string.leaderboard_loading))

    private fun fetchLeaderboard(mode: String, onResult: (RankUi) -> Unit) {
        Thread {
            val rank = runCatching { leaderboardUi(mode) }
                .getOrElse { RankUi(getString(R.string.leaderboard), getString(R.string.leaderboard_load_failed, it.message ?: it.javaClass.simpleName)) }
            runOnUiThread { onResult(rank) }
        }.start()
    }

    private fun leaderboardUi(mode: String): RankUi {
        val apiMode = rankModeApi(mode)
        val connection = (URL("$RANK_API_BASE_URL/leaderboard?mode=$apiMode").openConnection() as HttpURLConnection).apply {
            connectTimeout = 7000
            readTimeout = 7000
            setRequestProperty("Accept", "application/json")
            setRequestProperty("User-Agent", "BTCRig-Android")
        }
        return try {
            if (connection.responseCode !in 200..299) throw IllegalStateException("HTTP ${connection.responseCode}")
            val rows = JSONObject(connection.inputStream.bufferedReader().use { it.readText() }).optJSONArray("rows") ?: JSONArray()
            if (rows.length() == 0) return RankUi(rankModeLabel(apiMode), getString(R.string.leaderboard_empty))
            RankUi(
                title = rankModeLabel(apiMode),
                rows = buildList {
                    for (i in 0 until minOf(rows.length(), 50)) {
                    val row = rows.getJSONObject(i)
                    val recommended = row.optString("recommended", "")
                    val nameMode = if (apiMode == "all") rankModeApi(recommended.lowercase(Locale.US).replace("+", "_")) else apiMode
                    val name = leaderboardName(row, nameMode)
                    val rate = when (apiMode) {
                        "cpu" -> row.optDouble("cpu_hashrate")
                        "gpu" -> row.optDouble("gpu_hashrate")
                        "cpu_gpu" -> row.optDouble("cpu_gpu_hashrate")
                        else -> row.optDouble("max_hashrate")
                    }
                        add(RankUiRow(row.optInt("rank", i + 1), name.ifBlank { getString(R.string.unknown_device) }, formatHashrate(rate)))
                    }
                },
            )
        } finally {
            connection.disconnect()
        }
    }

    private fun rankModeApi(mode: String): String = when (mode) {
        "cpu", "gpu", "cpu_gpu" -> mode
        else -> "all"
    }

    private fun rankModeLabel(mode: String): String = when (rankModeApi(mode)) {
        "cpu" -> getString(R.string.rank_cpu)
        "gpu" -> getString(R.string.rank_gpu)
        "cpu_gpu" -> getString(R.string.rank_cpu_gpu)
        else -> getString(R.string.rank_general)
    }

    private fun leaderboardName(row: JSONObject, mode: String): String {
        val soc = row.optString("soc_name").ifBlank { row.optString("device_name") }
        val gpu = shortGpuName(row.optString("gpu_name"))
        return when (mode) {
            "gpu" -> gpu.ifBlank { soc }
            "cpu" -> soc
            else -> listOf(soc, gpu).filter { it.isNotBlank() && !it.startsWith("Unknown ") }.joinToString(" · ").ifBlank { soc.ifBlank { gpu } }
        }
    }

    private fun shortGpuName(name: String): String = name
        .replace("QUALCOMM ", "", ignoreCase = true)
        .replace("OpenCL 3.0 ", "", ignoreCase = true)
        .replace("OpenCL 2.0 ", "", ignoreCase = true)
        .replace("OpenCL ", "", ignoreCase = true)
        .trim()

    private fun submitBenchmark(cpuHps: Double, gpuHps: Double, cpuGpuHps: Double): String {
        val opencl = BtcrigNative.openclStatus(runCatching { BtcrigConfig.ensure(this).absolutePath }.getOrDefault(""))
        val gpu = parseOpencl(opencl)
        val json = JSONObject()
            .put("app_version", versionName())
            .put("android_version", "${Build.VERSION.RELEASE} API ${Build.VERSION.SDK_INT}")
            .put("brand", Build.BRAND)
            .put("model", Build.MODEL)
            .put("device", Build.DEVICE)
            .put("hardware", Build.HARDWARE)
            .put("soc_manufacturer", if (Build.VERSION.SDK_INT >= 31) Build.SOC_MANUFACTURER else "")
            .put("soc_model", if (Build.VERSION.SDK_INT >= 31) Build.SOC_MODEL else Build.HARDWARE)
            .put("cpu_abi", Build.SUPPORTED_ABIS.firstOrNull().orEmpty())
            .put("cpu_cores", Runtime.getRuntime().availableProcessors().coerceAtLeast(1))
            .put("gpu_name", gpu.name)
            .put("opencl_version", gpu.version)
            .put("cpu_hashrate", cpuHps.coerceAtLeast(0.0))
            .put("gpu_hashrate", gpuHps.coerceAtLeast(0.0))
            .put("cpu_gpu_hashrate", cpuGpuHps.coerceAtLeast(0.0))
            .put("duration_sec", BENCHMARK_SECONDS)
            .put("install_id", installId())
        val connection = (URL("$RANK_API_BASE_URL/benchmarks").openConnection() as HttpURLConnection).apply {
            requestMethod = "POST"
            connectTimeout = 7000
            readTimeout = 7000
            doOutput = true
            setRequestProperty("Content-Type", "application/json")
            setRequestProperty("Accept", "application/json")
            setRequestProperty("User-Agent", "BTCRig-Android")
        }
        return try {
            connection.outputStream.use { it.write(json.toString().toByteArray(Charsets.UTF_8)) }
            val body = (if (connection.responseCode in 200..299) connection.inputStream else connection.errorStream)
                ?.bufferedReader()
                ?.use { it.readText() }
                .orEmpty()
            if (connection.responseCode !in 200..299) throw IllegalStateException("HTTP ${connection.responseCode}: $body")
            val res = JSONObject(body)
            if (res.optBoolean("accepted")) getString(R.string.leaderboard_uploaded)
            else getString(R.string.leaderboard_rejected, res.optString("reject_reason", "rejected"))
        } finally {
            connection.disconnect()
        }
    }

    private fun cpuSummary(): String {
        val model = if (Build.VERSION.SDK_INT >= 31) {
            listOf(Build.SOC_MANUFACTURER, Build.SOC_MODEL).filter { it.isNotBlank() }.joinToString(" ")
        } else {
            runCatching {
                File("/proc/cpuinfo").readLines()
                    .firstOrNull { it.startsWith("Hardware") || it.startsWith("Processor") || it.startsWith("model name") }
                    ?.substringAfter(':')
                    ?.trim()
            }.getOrNull().orEmpty()
        }.ifBlank { Build.HARDWARE.ifBlank { getString(R.string.unknown_cpu) } }
        return getString(R.string.cpu_summary_value, model, Runtime.getRuntime().availableProcessors(), Build.SUPPORTED_ABIS.firstOrNull().orEmpty())
    }

    private fun gpuSummary(opencl: String): String {
        val device = opencl.lineSequence().firstOrNull { it.startsWith("#0 ") } ?: return opencl
            .lineSequence()
            .firstOrNull { it.startsWith("Runtime:") || it.startsWith("Mode:") }
            ?.substringAfter(':')
            ?.trim()
            ?.ifBlank { getString(R.string.no_opencl_device) }
            ?: getString(R.string.no_opencl_device)
        val parts = device.split('/').map { it.trim() }
        val name = parts.getOrNull(2).orEmpty().ifBlank { parts.getOrNull(1).orEmpty() }
        val api = parts.getOrNull(3).orEmpty()
        return api.ifBlank { name }.ifBlank { device }
    }

    private fun parseOpencl(opencl: String): OpenclInfo {
        val line = opencl.lineSequence().firstOrNull { it.startsWith("#0 ") }.orEmpty()
        val parts = line.split('/').map { it.trim() }
        return OpenclInfo(parts.getOrNull(2).orEmpty(), parts.getOrNull(3).orEmpty())
    }

    @Suppress("DEPRECATION")
    private fun versionName(): String = runCatching {
        packageManager.getPackageInfo(packageName, 0).versionName ?: "0.1.0"
    }.getOrDefault("0.1.0")
}

private data class UiState(
    val version: String,
    val backend: String,
    val selfTest: Boolean,
    val running: Boolean,
    val service: String,
    val hashrate: String,
    val workers: String,
    val total: String,
    val pool: String,
    val stratum: String,
    val shares: String,
    val error: String,
    val opencl: String,
    val cpuSummary: String,
    val gpuSummary: String,
    val configSummary: String,
    val configPath: String,
    val logPath: String,
)

private data class UpdateState(
    val latestVersion: String = "",
    val url: String = "",
    val available: Boolean = false,
    val checking: Boolean = false,
    val error: String = "",
)

private data class ReleaseInfo(val version: String, val url: String)

private data class OpenclInfo(val name: String, val version: String)

private data class RankUi(val title: String, val message: String = "", val rows: List<RankUiRow> = emptyList())

private data class RankUiRow(val rank: Int, val name: String, val rate: String)

private val Ink = Color(0xFF172033)
private val Muted = Color(0xFF6E7890)
private val Accent = Color(0xFF26364F)
private val RigBlue = Color(0xFF4C6F9F)
private val SoftBlue = Color(0xFFE8EDF8)
private val CardFill = Color(0xFFEFF1F7)
private val FieldFill = Color.Transparent
private const val BENCHMARK_SECONDS = 3
private val DONATION_LEVELS = listOf(0, 1, 3, 5, 99)
private const val RANK_API_BASE_URL = "https://www.btcrig.net/api/v1"
private const val UPDATE_API_URL = "https://api.github.com/repos/lxzcl/BTCRig-Android/releases/latest"
private const val UPDATE_RELEASE_URL = "https://github.com/lxzcl/BTCRig-Android/releases/latest"
private const val UPDATE_CACHE_MS = 6 * 60 * 60 * 1000L

@Composable
private fun BtcrigTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = lightColorScheme(
            primary = Accent,
            secondary = Muted,
            surface = Color.White,
            background = Color(0xFFF5F7FB),
            onPrimary = Color.White,
            onSurface = Ink,
            onBackground = Ink,
        ),
        content = content,
    )
}

@Preview(name = "Home", showBackground = true, widthDp = 390, heightDp = 844)
@Composable
private fun HomePreview() = PreviewScreen(0)

@Preview(name = "Settings", showBackground = true, widthDp = 390, heightDp = 844)
@Composable
private fun SettingsPreview() = PreviewScreen(1)

@Preview(name = "Info", showBackground = true, widthDp = 390, heightDp = 844)
@Composable
private fun InfoPreview() = PreviewScreen(2)

@Preview(name = "Rank", showBackground = true, widthDp = 390, heightDp = 844)
@Composable
private fun RankPreview() = PreviewScreen(3)

@Composable
private fun PreviewScreen(page: Int) {
    BtcrigTheme {
        BtcrigScreen(
            ui = previewUi(),
            update = UpdateState(latestVersion = "0.1.3", available = true),
            page = page,
            benchmark = "Benchmark\nCPU full cores: 8\n\nCPU: --\nGPU: --\nCPU + GPU: --",
            rankMode = "all",
            leaderboard = RankUi("all", rows = listOf(RankUiRow(1, "QTI SM8650 · Adreno(TM) 750", "141.30 MH/s"))),
            benchmarking = false,
            onPage = {},
            onRankMode = {},
            onOpenUpdate = {},
            onStart = {},
            onStop = {},
            onBenchmark = {},
            basic = previewBasic(),
            onBasicChange = {},
            onBatteryOptimization = {},
            onJson = {},
            onLog = {},
        )
    }
}

@Composable
private fun BtcrigScreen(
    ui: UiState,
    update: UpdateState,
    page: Int,
    benchmark: String,
    rankMode: String,
    leaderboard: RankUi,
    benchmarking: Boolean,
    onPage: (Int) -> Unit,
    onRankMode: (String) -> Unit,
    onOpenUpdate: () -> Unit,
    onStart: () -> Unit,
    onStop: () -> Unit,
    onBenchmark: () -> Unit,
    basic: BtcrigConfig.Basic,
    onBasicChange: (BtcrigConfig.Basic) -> Unit,
    onBatteryOptimization: () -> Unit,
    onJson: () -> Unit,
    onLog: () -> Unit,
) {
    Scaffold(
        bottomBar = {
            BottomNav(page, onPage)
        },
    ) { padding ->
        Surface(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding),
            color = MaterialTheme.colorScheme.background,
        ) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(start = 24.dp, end = 24.dp, top = 24.dp)
            ) {
                Crossfade(targetState = page, animationSpec = tween(220), label = "page") { currentPage ->
                    when (currentPage) {
                        1 -> {
                            Column(
                                modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()),
                                verticalArrangement = Arrangement.spacedBy(16.dp),
                            ) {
                                PageHeader()
                                SettingsPage(ui, basic, onBasicChange, onBatteryOptimization, onJson)
                            }
                        }
                        2 -> {
                            Column(
                                modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()),
                                verticalArrangement = Arrangement.spacedBy(16.dp),
                            ) {
                                PageHeader()
                                InfoPage(ui, update, benchmark, benchmarking, onBenchmark, onLog, basic, onBasicChange)
                            }
                        }
                        3 -> {
                            Column(
                                modifier = Modifier.fillMaxSize(),
                                verticalArrangement = Arrangement.spacedBy(16.dp),
                            ) {
                                PageHeader()
                                RankPage(leaderboard, rankMode, onRankMode)
                            }
                        }
                        else -> HomePage(
                            ui = ui,
                            update = update,
                            onOpenUpdate = onOpenUpdate,
                            onStart = onStart,
                            onStop = onStop,
                            modifier = Modifier.fillMaxSize(),
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun BottomNav(page: Int, onPage: (Int) -> Unit) {
    Surface(
        color = MaterialTheme.colorScheme.surface,
        modifier = Modifier.fillMaxWidth()
    ) {
        Column {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(0.5.dp)
                    .background(Color(0xFFE2E5EC))
            )
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .navigationBarsPadding()
                    .padding(horizontal = 24.dp, vertical = 12.dp)
            ) {
                listOf(
                    stringResource(R.string.tab_home),
                    stringResource(R.string.tab_settings),
                    stringResource(R.string.tab_info),
                    stringResource(R.string.tab_rank),
                ).forEachIndexed { index, title ->
                    Column(
                        modifier = Modifier
                            .weight(1f)
                            .clip(RoundedCornerShape(18.dp))
                            .clickable { onPage(index) }
                            .padding(vertical = 8.dp),
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        Box(
                            modifier = Modifier
                                .width(52.dp)
                                .height(8.dp)
                                .clip(RoundedCornerShape(999.dp))
                                .background(if (page == index) Accent else Color.Transparent),
                        )
                        Text(
                            title,
                            color = if (page == index) Accent else MaterialTheme.colorScheme.secondary,
                            fontWeight = if (page == index) FontWeight.Bold else FontWeight.Normal,
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun HomePage(
    ui: UiState,
    update: UpdateState,
    onOpenUpdate: () -> Unit,
    onStart: () -> Unit,
    onStop: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Box(modifier = modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.fillMaxWidth(),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(24.dp),
        ) {
            EnterUp(delayMillis = 0) {
                Box(
                    modifier = Modifier.fillMaxWidth(),
                    contentAlignment = Alignment.Center,
                ) {
                    HeroGlow(ui.running)
                    Column(
                        modifier = Modifier.offset(y = (-18).dp),
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.spacedBy(18.dp),
                    ) {
                        Box(modifier = Modifier.height(74.dp))
                        HashrateText(ui.hashrate)
                    }
                }
            }
            EnterUp(delayMillis = 180) {
                StatusPill(
                    running = ui.running,
                    text = when {
                        ui.running -> stringResource(R.string.status_running)
                        ui.service == "missing" -> stringResource(R.string.status_service_missing)
                        else -> stringResource(R.string.status_stopped)
                    },
                    onClick = { if (ui.running) onStop() else onStart() },
                )
            }
            EnterUp(delayMillis = 320) {
                Column(modifier = Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    SpecRow("CPU", ui.cpuSummary)
                    SpecRow("GPU", ui.gpuSummary)
                    SpecRow(stringResource(R.string.pool_label), ui.pool)
                }
            }
            if (ui.error.isNotBlank()) {
                EnterUp(delayMillis = 420) {
                    AppCard(stringResource(R.string.last_error)) { Line(ui.error) }
                }
            }
        }
        Box(modifier = Modifier.align(Alignment.BottomCenter).fillMaxWidth()) {
            EnterUp(delayMillis = 520) {
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(bottom = 8.dp),
                    contentAlignment = Alignment.BottomCenter,
                ) {
                    BrandHeader(ui.version, update, onOpenUpdate)
                }
            }
        }
    }
}

@Composable
private fun PageHeader() {
    BrandTitle(42)
}

@Composable
private fun BrandHeader(version: String, update: UpdateState, onOpenUpdate: () -> Unit) {
    Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Row(
            modifier = Modifier
                .clip(RoundedCornerShape(999.dp))
                .clickable(enabled = update.available, onClick = onOpenUpdate)
                .padding(horizontal = 4.dp, vertical = 2.dp),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            UpdateBadge(false)
            Text("v$version", color = MaterialTheme.colorScheme.secondary, fontSize = 13.sp)
            UpdateBadge(update.available)
        }
        BrandTitle(18)
    }
}

@Composable
private fun BrandTitle(size: Int) {
    Text(
        buildAnnotatedString {
            withStyle(SpanStyle(color = Ink)) { append("BTC") }
            withStyle(SpanStyle(color = RigBlue)) { append("Rig") }
        },
        fontSize = size.sp,
        fontWeight = FontWeight.ExtraBold,
    )
}

@Composable
private fun UpdateBadge(visible: Boolean) {
    val transition = rememberInfiniteTransition(label = "update-badge")
    val pulse by transition.animateFloat(
        initialValue = 0.68f,
        targetValue = 1f,
        animationSpec = infiniteRepeatable(animation = tween(1100), repeatMode = RepeatMode.Reverse),
        label = "update-badge-pulse",
    )
    Box(
        modifier = Modifier
            .width(36.dp)
            .height(18.dp)
            .graphicsLayer { alpha = if (visible) pulse else 0f }
            .clip(RoundedCornerShape(999.dp))
            .background(SoftBlue),
        contentAlignment = Alignment.Center,
    ) {
        Text("NEW", modifier = Modifier.offset(y = (-1).dp), color = Accent, fontSize = 9.sp, fontWeight = FontWeight.Bold)
    }
}

@Composable
private fun EnterUp(delayMillis: Int, content: @Composable () -> Unit) {
    var visible by remember { mutableStateOf(false) }
    LaunchedEffect(Unit) { visible = true }
    AnimatedVisibility(
        visible = visible,
        enter = fadeIn(animationSpec = tween(560, delayMillis)) +
            slideInVertically(animationSpec = tween(560, delayMillis)) { it / 4 },
    ) {
        content()
    }
}

@Composable
private fun HashrateText(text: String) {
    val split = text.lastIndexOf(' ')
    val value = if (split > 0) text.substring(0, split) else text
    val unit = if (split > 0) text.substring(split + 1) else ""
    val numeric = value.toFloatOrNull()
    val animated by animateFloatAsState(
        targetValue = numeric ?: 0f,
        animationSpec = tween(700),
        label = "hashrate-value",
    )
    val displayValue = if (numeric == null) {
        value
    } else if (unit == "H/s") {
        String.format(Locale.US, "%.0f", animated)
    } else {
        String.format(Locale.US, "%.2f", animated)
    }
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.Center,
        verticalAlignment = Alignment.Bottom,
    ) {
        if (unit.isNotBlank()) {
            Text(
                unit,
                modifier = Modifier
                    .padding(end = 8.dp, bottom = 10.dp)
                    .graphicsLayer { alpha = 0f },
                fontSize = 14.sp,
                fontWeight = FontWeight.Bold,
            )
        }
        Text(displayValue, fontSize = 56.sp, fontWeight = FontWeight.Bold)
        if (unit.isNotBlank()) {
            Text(
                unit,
                modifier = Modifier.padding(start = 8.dp, bottom = 10.dp),
                fontSize = 14.sp,
                fontWeight = FontWeight.Bold,
                color = MaterialTheme.colorScheme.secondary,
            )
        }
    }
}

@Composable
private fun HeroGlow(running: Boolean) {
    val transition = rememberInfiniteTransition(label = "hero")
    val pulse by transition.animateFloat(
        initialValue = 0.9f,
        targetValue = 1.12f,
        animationSpec = infiniteRepeatable(animation = tween(1600), repeatMode = RepeatMode.Reverse),
        label = "hero-pulse",
    )
    Box(
        modifier = Modifier
            .size(280.dp)
            .graphicsLayer {
                alpha = if (running) 0.8f else 0.55f
                scaleX = pulse
                scaleY = pulse
            }
            .clip(CircleShape)
            .background(
                Brush.radialGradient(
                    listOf(
                        if (running) Color(0x3318A058) else Color(0x3326364F),
                        Color.Transparent,
                    ),
                ),
            ),
    )
}

@Composable
private fun StatusPill(running: Boolean, text: String, onClick: () -> Unit) {
    val transition = rememberInfiniteTransition(label = "status")
    val alpha by transition.animateFloat(
        initialValue = 0.35f,
        targetValue = 1f,
        animationSpec = infiniteRepeatable(animation = tween(900), repeatMode = RepeatMode.Reverse),
        label = "status-alpha",
    )
    val scale by transition.animateFloat(
        initialValue = 0.85f,
        targetValue = 1.15f,
        animationSpec = infiniteRepeatable(animation = tween(900), repeatMode = RepeatMode.Reverse),
        label = "status-scale",
    )
    val color = if (running) Color(0xFF18A058) else Accent
    Row(
        modifier = Modifier
            .clip(RoundedCornerShape(999.dp))
            .background(if (running) Color(0xFFDFF8E9) else SoftBlue)
            .clickable(onClick = onClick)
            .padding(horizontal = 24.dp, vertical = 14.dp),
        horizontalArrangement = Arrangement.spacedBy(10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            modifier = Modifier
                .size(10.dp)
                .graphicsLayer {
                    this.alpha = alpha
                    scaleX = scale
                    scaleY = scale
                }
                .clip(CircleShape)
                .background(color),
        )
        Text(text, color = color, fontWeight = FontWeight.Medium)
    }
}

@Composable
private fun SpecRow(name: String, value: String) {
    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        Text(
            name,
            modifier = Modifier.weight(0.8f),
            color = MaterialTheme.colorScheme.secondary,
            fontSize = 13.sp,
        )
        Text(
            value,
            modifier = Modifier.weight(2f),
            textAlign = TextAlign.End,
            fontSize = 13.sp,
            lineHeight = 18.sp,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

@Composable
private fun SettingsPage(
    ui: UiState,
    basic: BtcrigConfig.Basic,
    onBasicChange: (BtcrigConfig.Basic) -> Unit,
    onBatteryOptimization: () -> Unit,
    onJson: () -> Unit,
) {
    val enabled = !ui.running
    SettingSection(stringResource(R.string.settings_title), compact = true) {
        SettingField(
            value = basic.poolUrl,
            onValueChange = { onBasicChange(basic.copyBasic(poolUrl = it)) },
            label = stringResource(R.string.pool_url),
            enabled = enabled,
        )
        SettingField(
            value = basic.user,
            onValueChange = { onBasicChange(basic.copyBasic(user = it)) },
            label = stringResource(R.string.user_worker),
            enabled = enabled,
        )
        SettingField(
            value = basic.pass,
            onValueChange = { onBasicChange(basic.copyBasic(pass = it)) },
            label = stringResource(R.string.password),
            enabled = enabled,
        )
        SettingField(
            value = basic.cpuThreads.toString(),
            onValueChange = { onBasicChange(basic.copyBasic(cpuThreads = it.filter(Char::isDigit).toIntOrNull() ?: 0)) },
            label = stringResource(R.string.cpu_threads),
            enabled = enabled,
            helper = stringResource(R.string.cpu_threads_helper),
        )
        SettingField(
            value = basic.difficulty.toString(),
            onValueChange = { onBasicChange(basic.copyBasic(difficulty = it.toDoubleOrNull() ?: 0.0)) },
            label = stringResource(R.string.difficulty),
            enabled = enabled,
            helper = stringResource(R.string.difficulty_helper),
        )
        SettingSwitchRow(stringResource(R.string.enable_opencl_gpu), basic.openclEnabled, enabled) {
            onBasicChange(basic.copyBasic(openclEnabled = it))
        }
        SettingSwitchRow(stringResource(R.string.allow_unknown_certs), basic.certCompat, enabled) {
            onBasicChange(basic.copyBasic(certCompat = it))
        }
        SettingSwitchRow(stringResource(R.string.keep_awake), basic.wakeLock, enabled) {
            onBasicChange(basic.copyBasic(wakeLock = it))
        }
        if (!enabled) {
            Line(stringResource(R.string.stop_service_before_save))
        }
    }
    RigButton(text = stringResource(R.string.ignore_battery_optimizations), onClick = onBatteryOptimization)
    RigButton(text = stringResource(R.string.advanced_json), onClick = onJson, enabled = enabled)
}

@Composable
private fun SettingSection(title: String, compact: Boolean = false, content: @Composable ColumnScope.() -> Unit) {
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text(
            title,
            color = RigBlue,
            fontSize = 13.sp,
            fontWeight = FontWeight.Medium,
        )
        SoftCard(compact, content)
    }
}

@Composable
private fun SettingField(
    value: String,
    onValueChange: (String) -> Unit,
    label: String,
    enabled: Boolean,
    helper: String = "",
) {
    Column(modifier = Modifier.padding(vertical = 4.dp), verticalArrangement = Arrangement.spacedBy(1.dp)) {
        Text(label, color = MaterialTheme.colorScheme.secondary, fontSize = 15.sp, lineHeight = 20.sp)
        BasicTextField(
            value = value,
            onValueChange = onValueChange,
            singleLine = true,
            enabled = enabled,
            modifier = Modifier.fillMaxWidth(),
            textStyle = TextStyle(fontSize = 14.sp, lineHeight = 18.sp, color = Ink),
        )
        if (helper.isNotBlank()) {
            Text(
                helper,
                modifier = Modifier.padding(top = 1.dp),
                color = MaterialTheme.colorScheme.secondary,
                fontSize = 12.sp,
                lineHeight = 16.sp,
            )
        }
    }
}

@Composable
private fun SettingSwitchRow(
    title: String,
    checked: Boolean,
    enabled: Boolean,
    onCheckedChange: (Boolean) -> Unit,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(enabled = enabled) { onCheckedChange(!checked) }
            .padding(vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text(title, modifier = Modifier.weight(1f), color = MaterialTheme.colorScheme.secondary, fontSize = 15.sp, lineHeight = 20.sp)
        Switch(
            checked = checked,
            onCheckedChange = if (enabled) onCheckedChange else null,
            enabled = enabled,
            colors = SwitchDefaults.colors(
                checkedThumbColor = Color.White,
                checkedTrackColor = Accent,
                uncheckedThumbColor = Muted,
                uncheckedTrackColor = Color(0xFFE2E5EC),
            ),
        )
    }
}

@Composable
private fun InfoPage(
    ui: UiState,
    update: UpdateState,
    benchmark: String,
    benchmarking: Boolean,
    onBenchmark: () -> Unit,
    onLog: () -> Unit,
    basic: BtcrigConfig.Basic,
    onBasicChange: (BtcrigConfig.Basic) -> Unit,
) {
    Text(stringResource(R.string.info_title), color = RigBlue, fontSize = 13.sp, fontWeight = FontWeight.Medium)
    RigButton(
        text = if (benchmarking) stringResource(R.string.benchmarking) else stringResource(R.string.benchmark),
        onClick = onBenchmark,
        enabled = !benchmarking && !ui.running
    )
    BenchmarkBox(benchmark)
    RigButton(text = stringResource(R.string.view_log), onClick = onLog)
    SoftCard(compact = true) {
        Line(updateText(update))
        Line("${stringResource(R.string.recent_error)}: ${ui.error.ifBlank { stringResource(R.string.no_recent_errors) }}")
        Line("${stringResource(R.string.backend)}: ${ui.backend}")
        Line("${stringResource(R.string.self_test)}: ${if (ui.selfTest) stringResource(R.string.ok) else stringResource(R.string.failed)}")
        Line("${stringResource(R.string.opencl_label)}:\n${ui.opencl}")
        Line(stringResource(R.string.config_log_value, ui.configPath, ui.logPath))
    }
    DonationCard(
        percent = basic.donationPercent,
        enabled = !ui.running,
        onChange = { onBasicChange(basic.copyBasic(donationPercent = it)) },
    )
}

@Composable
private fun ColumnScope.RankPage(
    leaderboard: RankUi,
    rankMode: String,
    onRankMode: (String) -> Unit,
) {
    Text(stringResource(R.string.leaderboard), color = RigBlue, fontSize = 13.sp, fontWeight = FontWeight.Medium)
    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        listOf(
            "all" to stringResource(R.string.rank_general),
            "cpu" to stringResource(R.string.rank_cpu),
            "gpu" to stringResource(R.string.rank_gpu),
            "cpu_gpu" to stringResource(R.string.rank_cpu_gpu),
        ).forEach { (mode, label) ->
            RankModeButton(label, rankMode == mode) { onRankMode(mode) }
        }
    }
    RankBox(leaderboard, Modifier.weight(1f))
}

@Composable
private fun RowScope.RankModeButton(text: String, selected: Boolean, onClick: () -> Unit) {
    Surface(
        onClick = onClick,
        modifier = Modifier.weight(1f),
        shape = RoundedCornerShape(999.dp),
        color = if (selected) SoftBlue else Color.White,
        border = androidx.compose.foundation.BorderStroke(1.dp, if (selected) RigBlue else Color(0xFFE2E5EC)),
    ) {
        Text(
            text,
            modifier = Modifier.padding(vertical = 9.dp),
            textAlign = TextAlign.Center,
            color = if (selected) Accent else Muted,
            fontSize = 13.sp,
            fontWeight = if (selected) FontWeight.Bold else FontWeight.Normal,
        )
    }
}

@Composable
private fun RankBox(rank: RankUi, modifier: Modifier = Modifier) {
    Card(
        modifier = modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = CardFill),
        elevation = CardDefaults.cardElevation(defaultElevation = 0.dp),
        shape = RoundedCornerShape(22.dp),
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(14.dp)
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            if (rank.message.isNotBlank()) {
                Text(rank.message, color = MaterialTheme.colorScheme.secondary, fontSize = 14.sp, lineHeight = 20.sp)
            }
            rank.rows.forEach { RankLine(it) }
        }
    }
}

@Composable
private fun RankLine(row: RankUiRow) {
    Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Text(
            "#${row.rank} ${row.name}",
            modifier = Modifier.weight(1f).padding(end = 10.dp),
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            color = MaterialTheme.colorScheme.secondary,
            fontSize = 13.sp,
        )
        Text(
            row.rate,
            color = Ink,
            fontSize = 13.sp,
            fontWeight = FontWeight.Medium,
            maxLines = 1,
        )
    }
}

@Composable
private fun updateText(update: UpdateState): String = when {
    update.checking -> stringResource(R.string.update_checking)
    update.available -> stringResource(R.string.update_available, update.latestVersion)
    update.error.isNotBlank() -> stringResource(R.string.update_check_failed, update.error)
    update.latestVersion.isNotBlank() -> stringResource(R.string.update_current)
    else -> stringResource(R.string.update_unknown)
}

@Composable
private fun DonationCard(percent: Int, enabled: Boolean, onChange: (Int) -> Unit) {
    val index = DONATION_LEVELS.indexOf(percent).takeIf { it >= 0 } ?: DONATION_LEVELS.indexOf(1)
    SettingSection(stringResource(R.string.support_author), compact = true) {
        Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.donation_ratio), modifier = Modifier.weight(1f), color = MaterialTheme.colorScheme.secondary, fontSize = 15.sp)
            Text("${DONATION_LEVELS[index]}%", color = RigBlue, fontSize = 16.sp, fontWeight = FontWeight.Medium)
        }
        Slider(
            value = index.toFloat(),
            onValueChange = { onChange(DONATION_LEVELS[it.toInt().coerceIn(0, DONATION_LEVELS.lastIndex)]) },
            enabled = enabled,
            modifier = Modifier.fillMaxWidth().height(32.dp),
            valueRange = 0f..DONATION_LEVELS.lastIndex.toFloat(),
            steps = DONATION_LEVELS.size - 2,
            colors = SliderDefaults.colors(
                thumbColor = RigBlue,
                activeTrackColor = RigBlue,
                inactiveTrackColor = SoftBlue,
                activeTickColor = Color.Transparent,
                inactiveTickColor = RigBlue,
            ),
        )
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            DONATION_LEVELS.forEach { level ->
                Text("$level%", color = Muted, fontSize = 12.sp)
            }
        }
    }
}

@Composable
private fun BenchmarkBox(text: String) {
    SoftCard(compact = true) {
        Text(
            text,
            modifier = Modifier
                .fillMaxWidth()
                .height(144.dp)
                .verticalScroll(rememberScrollState()),
            color = MaterialTheme.colorScheme.secondary,
            fontFamily = FontFamily.Monospace,
            fontSize = 14.sp,
            lineHeight = 18.sp,
        )
    }
}

@Composable
private fun RigButton(
    text: String,
    onClick: () -> Unit,
    enabled: Boolean = true,
) {
    Surface(
        onClick = onClick,
        enabled = enabled,
        shape = RoundedCornerShape(12.dp),
        color = if (enabled) Color.White else Color(0xFFE2E5EC),
        border = if (enabled) androidx.compose.foundation.BorderStroke(1.dp, Color(0xFFE2E5EC)) else null,
    ) {
        Text(
            text = text,
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 9.dp),
            color = if (enabled) RigBlue else Muted,
            fontWeight = FontWeight.Bold,
            fontSize = 14.sp
        )
    }
}

@Composable
private fun SoftCard(compact: Boolean = false, content: @Composable ColumnScope.() -> Unit) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = CardFill),
        elevation = CardDefaults.cardElevation(defaultElevation = 0.dp),
        shape = RoundedCornerShape(22.dp),
    ) {
        Column(
            modifier = Modifier.padding(if (compact) 14.dp else 20.dp),
            verticalArrangement = Arrangement.spacedBy(if (compact) 7.dp else 12.dp),
        ) {
            content()
        }
    }
}

@Composable
private fun AppCard(title: String, content: @Composable ColumnScope.() -> Unit) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = CardFill),
        elevation = CardDefaults.cardElevation(defaultElevation = 0.dp),
        shape = RoundedCornerShape(22.dp),
    ) {
        Column(modifier = Modifier.padding(20.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(title, fontSize = 18.sp, fontWeight = FontWeight.Bold)
            content()
        }
    }
}

@Composable
private fun Line(text: String) {
    Text(text, color = MaterialTheme.colorScheme.secondary, fontSize = 14.sp, lineHeight = 18.sp)
}

@Composable
private fun JsonDialog(text: String, onDismiss: () -> Unit, onSave: (String) -> Unit) {
    var json by remember { mutableStateOf(text) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.edit_config_json)) },
        text = {
            OutlinedTextField(
                value = json,
                onValueChange = { json = it },
                modifier = Modifier
                    .fillMaxWidth()
                    .height(360.dp),
            )
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.cancel)) } },
        confirmButton = { Button(onClick = { onSave(json) }) { Text(stringResource(R.string.save)) } },
    )
}

@Composable
private fun TextDialog(title: String, text: String, onCopy: () -> Unit, onDismiss: () -> Unit) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            Text(
                text,
                modifier = Modifier
                    .height(420.dp)
                    .verticalScroll(rememberScrollState()),
                fontSize = 12.sp,
            )
        },
        dismissButton = { TextButton(onClick = onCopy) { Text(stringResource(R.string.copy_log)) } },
        confirmButton = { Button(onClick = onDismiss) { Text(stringResource(R.string.close)) } },
    )
}

private fun readTail(
    file: File,
    maxBytes: Int,
    missing: String = "(log not found)",
    empty: String = "(empty log)",
    failed: (String) -> String = { "Log read failed: $it" },
): String {
    if (!file.exists()) return missing
    return runCatching {
        FileInputStream(file).use { input ->
            var skip = (file.length() - maxBytes).coerceAtLeast(0)
            while (skip > 0) {
                val skipped = input.skip(skip)
                if (skipped <= 0) break
                skip -= skipped
            }
            val buffer = ByteArray(minOf(maxBytes.toLong(), file.length()).toInt())
            val n = input.read(buffer)
            if (n > 0) cleanLog(String(buffer, 0, n)) else empty
        }
    }.getOrElse { failed(it.message.orEmpty()) }
}

private val AnsiEscape = Regex("\u001B\\[[0-9;]*[A-Za-z]")

private fun cleanLog(text: String): String = AnsiEscape.replace(text, "")

private fun recentLogError(file: File): String {
    for (line in readTail(file, 64 * 1024, "", "") { "" }
        .lineSequence()
        .map { it.trim() }
        .filter { it.isNotEmpty() }
        .toList()
        .asReversed()) {
        if (isRecoveryLogLine(line)) return ""
        if (isImportantLogLine(line)) return line
    }
    return ""
}

private fun isRecoveryLogLine(line: String): Boolean {
    val lower = line.lowercase(Locale.US)
    return "[stats] rate=" in lower ||
        ("[submit-rsp]" in lower && "accepted" in lower) ||
        "[authorize] ok" in lower
}

private fun isImportantLogLine(line: String): Boolean {
    val lower = line.lowercase(Locale.US)
    return "failed" in lower ||
        "error" in lower ||
        "invalid" in lower ||
        "exception" in lower ||
        "closed connection" in lower ||
        ("opencl" in lower && ("unavailable" in lower || "not found" in lower))
}

private fun fetchLatestRelease(): ReleaseInfo {
    val connection = (URL(UPDATE_API_URL).openConnection() as HttpURLConnection).apply {
        connectTimeout = 7000
        readTimeout = 7000
        setRequestProperty("Accept", "application/vnd.github+json")
        setRequestProperty("User-Agent", "BTCRig-Android")
    }
    return try {
        val code = connection.responseCode
        if (code !in 200..299) {
            throw IllegalStateException("HTTP $code")
        }
        val json = JSONObject(connection.inputStream.bufferedReader().use { reader -> reader.readText() })
        val version = normalizeVersion(json.optString("tag_name"))
        if (version.isBlank()) {
            throw IllegalStateException("missing tag_name")
        }
        ReleaseInfo(version, json.optString("html_url", UPDATE_RELEASE_URL))
    } finally {
        connection.disconnect()
    }
}

private fun updateStateFor(
    currentVersion: String,
    latestVersion: String,
    url: String,
    checking: Boolean = false,
): UpdateState {
    val latest = normalizeVersion(latestVersion)
    return UpdateState(
        latestVersion = latest,
        url = url,
        available = latest.isNotBlank() && compareVersions(latest, currentVersion) > 0,
        checking = checking,
    )
}

private fun normalizeVersion(version: String): String = version.trim().removePrefix("v").removePrefix("V")

private fun compareVersions(left: String, right: String): Int {
    val a = versionParts(left)
    val b = versionParts(right)
    for (i in 0 until 3) {
        if (a[i] != b[i]) return a[i].compareTo(b[i])
    }
    return 0
}

private fun versionParts(version: String): List<Int> {
    val parts = Regex("\\d+").findAll(version).map { it.value.toIntOrNull() ?: 0 }.take(3).toList()
    return parts + List(3 - parts.size) { 0 }
}

private fun cpuSummary(): String {
    val model = if (Build.VERSION.SDK_INT >= 31) {
        listOf(Build.SOC_MANUFACTURER, Build.SOC_MODEL).filter { it.isNotBlank() }.joinToString(" ")
    } else {
        runCatching {
            File("/proc/cpuinfo").readLines()
                .firstOrNull { it.startsWith("Hardware") || it.startsWith("Processor") || it.startsWith("model name") }
                ?.substringAfter(':')
                ?.trim()
        }.getOrNull().orEmpty()
    }.ifBlank { Build.HARDWARE.ifBlank { "Unknown CPU" } }
    return "$model · ${Runtime.getRuntime().availableProcessors()} cores · ${Build.SUPPORTED_ABIS.firstOrNull().orEmpty()}"
}

private fun gpuSummary(opencl: String): String {
    val device = opencl.lineSequence().firstOrNull { it.startsWith("#0 ") } ?: return opencl
        .lineSequence()
        .firstOrNull { it.startsWith("Runtime:") || it.startsWith("Mode:") }
        ?.substringAfter(':')
        ?.trim()
        ?.ifBlank { "No OpenCL device" }
        ?: "No OpenCL device"
    val parts = device.split('/').map { it.trim() }
    val name = parts.getOrNull(2).orEmpty().ifBlank { parts.getOrNull(1).orEmpty() }
    val api = parts.getOrNull(3).orEmpty()
    return api.ifBlank { name }.ifBlank { device }
}

private fun previewUi() = UiState(
    version = "0.1.0",
    backend = "arm-sha2",
    selfTest = true,
    running = true,
    service = "running",
    hashrate = "141.30 MH/s",
    workers = "Workers: 8",
    total = "Total: 123456789",
    pool = "stratum+tcp://public-pool.io:3333",
    stratum = "Stratum: authorized / connected: yes / jobs: 8",
    shares = "Shares: 3 submit / 3 ok / 0 reject",
    error = "",
    opencl = "Config: enabled\nRuntime: available\n#0 / Qualcomm / Adreno(TM) 750 / OpenCL 3.0",
    cpuSummary = "QTI SM8650 · 8 cores · arm64-v8a",
    gpuSummary = "OpenCL 3.0 Adreno(TM) 750",
    configSummary = "CPU: 8 threads / OpenCL: enabled",
    configPath = "/data/user/0/com.btcrig.android/files/config.json",
    logPath = "/data/user/0/com.btcrig.android/files/btcrig.log",
)

private fun previewBasic() = BtcrigConfig.Basic().apply {
    poolUrl = "stratum+tcp://public-pool.io:3333"
    user = "bc1qqz0wutk9kk5mmaf7fu4dm5w4fq4fhaah9hpzr3"
    pass = "x"
    cpuThreads = 8
    openclEnabled = true
    certCompat = true
    wakeLock = true
    donationPercent = 1
}

private fun BtcrigConfig.Basic.copyBasic(
    poolUrl: String = this.poolUrl,
    user: String = this.user,
    pass: String = this.pass,
    cpuThreads: Int = this.cpuThreads,
    difficulty: Double = this.difficulty,
    openclEnabled: Boolean = this.openclEnabled,
    certCompat: Boolean = this.certCompat,
    wakeLock: Boolean = this.wakeLock,
    donationPercent: Int = this.donationPercent,
): BtcrigConfig.Basic {
    val next = BtcrigConfig.Basic()
    next.poolUrl = poolUrl
    next.user = user
    next.pass = pass
    next.cpuThreads = cpuThreads.coerceAtLeast(0)
    next.difficulty = difficulty.coerceAtLeast(0.0)
    next.openclEnabled = openclEnabled
    next.certCompat = certCompat
    next.wakeLock = wakeLock
    next.donationPercent = DONATION_LEVELS.find { it == donationPercent } ?: 1
    return next
}

private fun formatHashrate(hps: Double): String = when {
    hps >= 1_000_000_000.0 -> String.format(Locale.US, "%.2f GH/s", hps / 1_000_000_000.0)
    hps >= 1_000_000.0 -> String.format(Locale.US, "%.2f MH/s", hps / 1_000_000.0)
    hps >= 1_000.0 -> String.format(Locale.US, "%.2f KH/s", hps / 1_000.0)
    else -> String.format(Locale.US, "%.0f H/s", hps)
}
