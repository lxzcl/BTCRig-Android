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
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import kotlinx.coroutines.delay
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest
import java.util.Locale
import java.util.UUID
import javax.crypto.Mac
import javax.crypto.spec.SecretKeySpec
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
            var uploadedBenchmark by remember { mutableStateOf(loadBenchmarkUploadStatus()) }
            var benchmarking by remember { mutableStateOf(false) }
            var uploadingBenchmark by remember { mutableStateOf(false) }
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
                    if (!benchmarking && !uploadingBenchmark) {
                        benchmark = loadBenchmarkText()
                        uploadedBenchmark = loadBenchmarkUploadStatus()
                    }
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

            LaunchedEffect(page) {
                if (page == 2 && !benchmarking && !uploadingBenchmark) {
                    benchmark = loadBenchmarkText()
                    uploadedBenchmark = loadBenchmarkUploadStatus()
                }
            }

            BtcrigTheme {
                BtcrigScreen(
                    ui = ui,
                    update = update,
                    page = page,
                    benchmark = benchmark,
                    uploadedBenchmark = uploadedBenchmark,
                    rankMode = rankMode,
                    leaderboard = leaderboard,
                    benchmarking = benchmarking,
                    uploadingBenchmark = uploadingBenchmark,
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
                        uploadedBenchmark = ""
                        saveBenchmarkUploadStatus("")
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
                            val result = lines.joinToString("\n")
                            saveBenchmarkText(result)
                            runOnUiThread {
                                benchmarking = false
                                benchmark = result
                                refreshLeaderboard()
                            }
                        }.start()
                    },
                    onUploadBenchmark = {
                        if (ui.running) {
                            toast(getString(R.string.stop_mining_before_benchmark))
                            return@BtcrigScreen
                        }
                        uploadingBenchmark = true
                        benchmark = getString(R.string.benchmark_upload_start)
                        Thread {
                            val status = runCatching {
                                runChallengeUpload { text -> runOnUiThread { benchmark = text } }
                            }.getOrElse { error ->
                                getString(R.string.leaderboard_upload_failed, error.message ?: error.javaClass.simpleName)
                            }
                            saveBenchmarkUploadStatus(status)
                            runOnUiThread {
                                uploadingBenchmark = false
                                uploadedBenchmark = status
                                benchmark = loadBenchmarkText()
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

    private fun loadBenchmarkUploadStatus(): String =
        getSharedPreferences("benchmark", MODE_PRIVATE).getString("upload_status", "").orEmpty()

    private fun saveBenchmarkUploadStatus(text: String) {
        getSharedPreferences("benchmark", MODE_PRIVATE).edit().putString("upload_status", text).apply()
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

    private fun benchmarkScoreSummary(result: BenchmarkResult): String =
        listOf(
            "CPU ${benchmarkRateText(result.cpuHps)}",
            "GPU ${benchmarkRateText(result.gpuHps)}",
            "CPU+GPU ${benchmarkRateText(result.cpuGpuHps)}",
        ).joinToString(" / ")

    private fun benchmarkRateText(rate: Double): String =
        if (rate >= 0.0) formatHashrate(rate) else getString(R.string.unavailable)

    private fun runChallengeUpload(onText: (String) -> Unit): String {
        val lines = mutableListOf(
            getString(R.string.upload_score),
            getString(R.string.benchmark_estimated_time),
            getString(R.string.benchmark_keep_open),
            "",
            getString(R.string.benchmark_challenge_requesting),
        )
        fun show(line: String) {
            lines.add(line)
            onText(lines.joinToString("\n"))
        }

        onText(lines.joinToString("\n"))
        val appSig = appSignatureHash()
        val challenge = createBenchmarkChallenge(appSig)
        show(getString(R.string.benchmark_challenge_ready))
        startBenchmarkChallenge(challenge)
        show(getString(R.string.benchmark_challenge_started))

        val configPath = runCatching { BtcrigConfig.ensure(this).absolutePath }.getOrDefault("")
        val threads = Runtime.getRuntime().availableProcessors().coerceAtLeast(1)
        val seconds = challenge.seconds.coerceAtLeast(UPLOAD_BENCHMARK_SECONDS)
        val startNs = System.nanoTime()
        show(getString(R.string.benchmark_backend_value, "CPU", getString(R.string.testing)))
        val cpuProof = parseBenchmarkProof(BtcrigNative.benchmarkCpuChallenge(challenge.seed, seconds, threads, challenge.proofDifficulty))
        show(getString(R.string.benchmark_backend_value, "CPU", benchmarkProofText(cpuProof)))
        show(getString(R.string.benchmark_backend_value, "GPU", getString(R.string.testing)))
        val gpuProof = parseBenchmarkProof(BtcrigNative.benchmarkOpenclChallenge(configPath, challenge.seed, seconds, challenge.proofDifficulty))
        show(getString(R.string.benchmark_backend_value, "GPU", benchmarkProofText(gpuProof)))
        show(getString(R.string.benchmark_backend_value, "CPU + GPU", getString(R.string.testing)))
        val cpuGpuProof = if (gpuProof.hps >= 0.0) parseBenchmarkProof(
            BtcrigNative.benchmarkCpuGpuChallenge(configPath, challenge.seed, seconds, threads, challenge.proofDifficulty)
        ) else BenchmarkProof()
        show(getString(R.string.benchmark_backend_value, "CPU + GPU", benchmarkProofText(cpuGpuProof)))

        val elapsedMs = (System.nanoTime() - startNs) / 1_000_000
        val result = BenchmarkResult(cpuProof.hps, gpuProof.hps, cpuGpuProof.hps, cpuProof, gpuProof, cpuGpuProof)
        show(getString(R.string.benchmark_challenge_submitting))
        val submit = finishBenchmarkChallenge(challenge, result, appSig, elapsedMs)
        return if (submit.accepted) getString(R.string.benchmark_uploaded_score, benchmarkScoreSummary(result)) else submit.text
    }

    private fun parseBenchmarkProof(text: String): BenchmarkProof {
        val json = JSONObject(text)
        return BenchmarkProof(
            json.optDouble("hashrate", -1.0),
            json.optLong("proof_nonce", 0),
            json.optString("proof_hash"),
            json.optBoolean("proof_found", false),
        )
    }

    private fun benchmarkProofText(proof: BenchmarkProof): String =
        if (proof.hps < 0.0) getString(R.string.unavailable)
        else if (proof.found) formatHashrate(proof.hps)
        else getString(R.string.benchmark_no_proof, formatHashrate(proof.hps))

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
            setRequestProperty("X-BTCRig-Install-ID", installId())
        }
        return try {
            if (connection.responseCode !in 200..299) throw IllegalStateException("HTTP ${connection.responseCode}")
            val body = JSONObject(connection.inputStream.bufferedReader().use { it.readText() })
            val rows = body.optJSONArray("rows") ?: JSONArray()
            val me = body.optJSONObject("me")?.let { rankRow(it, apiMode, it.optInt("rank", 0)) }
            if (rows.length() == 0) return RankUi(rankModeLabel(apiMode), getString(R.string.leaderboard_empty))
            RankUi(
                title = rankModeLabel(apiMode),
                me = me,
                rows = buildList {
                    for (i in 0 until minOf(rows.length(), 50)) {
                        add(rankRow(rows.getJSONObject(i), apiMode, i + 1))
                    }
                },
            )
        } finally {
            connection.disconnect()
        }
    }

    private fun rankRow(row: JSONObject, apiMode: String, fallbackRank: Int): RankUiRow {
        val recommended = row.optString("recommended", "")
        val nameMode = if (apiMode == "all") rankModeApi(recommended.lowercase(Locale.US).replace("+", "_")) else apiMode
        val name = leaderboardName(row, nameMode)
        val rate = when (apiMode) {
            "cpu" -> row.optDouble("cpu_hashrate")
            "gpu" -> row.optDouble("gpu_hashrate")
            "cpu_gpu" -> row.optDouble("cpu_gpu_hashrate")
            else -> when (nameMode) {
                "cpu" -> row.optDouble("cpu_hashrate")
                "gpu" -> row.optDouble("gpu_hashrate")
                "cpu_gpu" -> row.optDouble("cpu_gpu_hashrate")
                else -> row.optDouble("max_hashrate")
            }
        }
        return RankUiRow(
            row.optInt("rank", fallbackRank),
            name.ifBlank { getString(R.string.unknown_device) },
            formatHashrate(rate),
            rankDetail(row, name, rate),
        )
    }

    private fun rankDetail(row: JSONObject, name: String, rate: Double): String {
        fun line(label: Int, value: String): String =
            getString(R.string.rank_detail_line, getString(label), value.ifBlank { "--" })
        val samples = row.optInt("samples", 0)
        val signed = row.optInt("signed_samples", 0)
        val signHash = row.optString("app_signature_hash").take(16).ifBlank { "--" }
        return listOf(
            line(R.string.rank_detail_rank, "#${row.optInt("rank")}"),
            line(R.string.rank_detail_score, formatHashrate(rate)),
            line(R.string.rank_detail_recommended, row.optString("recommended")),
            line(R.string.rank_detail_samples, samples.toString()),
            line(R.string.rank_detail_signed_samples, "$signed/$samples"),
            line(R.string.rank_detail_device, row.optString("device_name").ifBlank { name }),
            line(R.string.rank_detail_soc, row.optString("soc_name")),
            line(R.string.rank_detail_gpu, shortGpuName(row.optString("gpu_name"))),
            line(R.string.rank_detail_cpu, formatHashrate(row.optDouble("cpu_hashrate"))),
            line(R.string.rank_detail_opencl, formatHashrate(row.optDouble("gpu_hashrate"))),
            line(R.string.rank_detail_cpu_gpu, formatHashrate(row.optDouble("cpu_gpu_hashrate"))),
            line(R.string.rank_detail_cpu_cores, row.optInt("cpu_cores", 0).toString()),
            line(R.string.rank_detail_threads, row.optInt("benchmark_threads", 0).toString()),
            line(R.string.rank_detail_abi, row.optString("cpu_abi")),
            line(R.string.rank_detail_opencl_version, row.optString("opencl_version")),
            line(R.string.rank_detail_android, row.optString("android_version")),
            line(R.string.rank_detail_app, row.optString("app_version")),
            line(R.string.rank_detail_signature, signHash),
            line(R.string.rank_detail_last_seen, row.optString("last_seen")),
        ).joinToString("\n")
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

    private fun createBenchmarkChallenge(appSig: String): BenchmarkChallenge {
        val res = postJson(
            "$RANK_API_BASE_URL/benchmark-challenges",
            JSONObject()
                .put("install_id", installId())
                .put("app_signature_hash", appSig),
        )
        val challenge = BenchmarkChallenge(
            res.optString("id"),
            res.optString("seed"),
            res.optString("token"),
            res.optInt("seconds", UPLOAD_BENCHMARK_SECONDS),
            res.optDouble("proof_difficulty", 0.0005),
        )
        if (challenge.id.isBlank() || challenge.seed.isBlank() || challenge.token.isBlank()) {
            throw IllegalStateException("bad challenge response")
        }
        return challenge
    }

    private fun startBenchmarkChallenge(challenge: BenchmarkChallenge) {
        postJson(
            "$RANK_API_BASE_URL/benchmark-challenges/${challenge.id}/start",
            JSONObject().put("token", challenge.token),
        )
    }

    private fun finishBenchmarkChallenge(
        challenge: BenchmarkChallenge,
        result: BenchmarkResult,
        appSig: String,
        elapsedMs: Long,
    ): SubmitResult {
        val opencl = BtcrigNative.openclStatus(runCatching { BtcrigConfig.ensure(this).absolutePath }.getOrDefault(""))
        val gpu = parseOpencl(opencl)
        val installId = installId()
        val nonce = "${System.currentTimeMillis()}-${UUID.randomUUID()}"
        val threads = Runtime.getRuntime().availableProcessors().coerceAtLeast(1)
        val checksum = challengeChecksum(challenge, result, appSig, threads, elapsedMs)
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
            .put("cpu_cores", threads)
            .put("gpu_name", gpu.name)
            .put("opencl_version", gpu.version)
            .put("cpu_hashrate", result.cpuHps.coerceAtLeast(0.0))
            .put("gpu_hashrate", result.gpuHps.coerceAtLeast(0.0))
            .put("cpu_gpu_hashrate", result.cpuGpuHps.coerceAtLeast(0.0))
            .put("duration_sec", challenge.seconds)
            .put("benchmark_threads", threads)
            .put("install_id", installId)
            .put("app_signature_hash", appSig)
            .put("nonce", nonce)
            .put("challenge_token", challenge.token)
            .put("challenge_checksum", checksum)
            .put("challenge_elapsed_ms", elapsedMs)
            .put("cpu_proof_nonce", result.cpuProof.nonce)
            .put("cpu_proof_hash", result.cpuProof.hash)
            .put("gpu_proof_nonce", result.gpuProof.nonce)
            .put("gpu_proof_hash", result.gpuProof.hash)
            .put("cpu_gpu_proof_nonce", result.cpuGpuProof.nonce)
            .put("cpu_gpu_proof_hash", result.cpuGpuProof.hash)
        val res = postJson("$RANK_API_BASE_URL/benchmark-challenges/${challenge.id}/finish", json, installId)
        return if (res.optBoolean("accepted")) SubmitResult(true, getString(R.string.leaderboard_uploaded))
        else SubmitResult(false, getString(R.string.leaderboard_rejected, res.optString("reject_reason", "rejected")))
    }

    private fun postJson(url: String, json: JSONObject, signatureKey: String = ""): JSONObject {
        val body = json.toString().toByteArray(Charsets.UTF_8)
        val connection = (URL(url).openConnection() as HttpURLConnection).apply {
            requestMethod = "POST"
            connectTimeout = 7000
            readTimeout = 15000
            doOutput = true
            setRequestProperty("Content-Type", "application/json")
            setRequestProperty("Accept", "application/json")
            setRequestProperty("User-Agent", "BTCRig-Android")
            if (signatureKey.isNotBlank()) {
                setRequestProperty("X-BTCRig-Signature", hmacSha256Hex(signatureKey, body))
            }
        }
        return try {
            connection.outputStream.use { it.write(body) }
            val text = (if (connection.responseCode in 200..299) connection.inputStream else connection.errorStream)
                ?.bufferedReader()
                ?.use { it.readText() }
                .orEmpty()
            if (connection.responseCode !in 200..299) throw IllegalStateException("HTTP ${connection.responseCode}: $text")
            JSONObject(text)
        } finally {
            connection.disconnect()
        }
    }

    private fun challengeChecksum(
        challenge: BenchmarkChallenge,
        result: BenchmarkResult,
        appSig: String,
        threads: Int,
        elapsedMs: Long,
    ): String = sha256Hex(
        listOf(
            "btcrig-challenge-v1",
            challenge.id,
            challenge.seed,
            appSig,
            challenge.seconds.toString(),
            threads.toString(),
            elapsedMs.toString(),
            challengeRate(result.cpuHps),
            challengeRate(result.gpuHps),
            challengeRate(result.cpuGpuHps),
            result.cpuProof.nonce.toString(),
            result.cpuProof.hash,
            result.gpuProof.nonce.toString(),
            result.gpuProof.hash,
            result.cpuGpuProof.nonce.toString(),
            result.cpuGpuProof.hash,
        ).joinToString("\n").toByteArray(Charsets.UTF_8)
    )

    private fun challengeRate(rate: Double): String =
        String.format(Locale.US, "%.3f", rate.coerceAtLeast(0.0))

    private fun appSignatureHash(): String = runCatching {
        val signatures = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            packageManager.getPackageInfo(packageName, PackageManager.GET_SIGNING_CERTIFICATES)
                .signingInfo
                ?.apkContentsSigners
        } else {
            @Suppress("DEPRECATION")
            packageManager.getPackageInfo(packageName, PackageManager.GET_SIGNATURES).signatures
        }
        signatures?.firstOrNull()?.toByteArray()?.let { sha256Hex(it) }.orEmpty()
    }.getOrDefault("")

    private fun hmacSha256Hex(key: String, data: ByteArray): String {
        val mac = Mac.getInstance("HmacSHA256")
        mac.init(SecretKeySpec(key.toByteArray(Charsets.UTF_8), "HmacSHA256"))
        return mac.doFinal(data).toHex()
    }

    private fun sha256Hex(data: ByteArray): String =
        MessageDigest.getInstance("SHA-256").digest(data).toHex()

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
