package com.btcrig.android

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.Crossfade
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
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
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TextField
import androidx.compose.material3.TextFieldDefaults
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
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
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.delay
import java.io.File
import java.io.FileInputStream
import java.util.Locale

class ModernActivity : ComponentActivity() {
    private var serviceState = ""

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        requestNotificationPermission()

        setContent {
            var ui by remember { mutableStateOf(readUi()) }
            var page by remember { mutableStateOf(0) }
            var showJson by remember { mutableStateOf(false) }
            var showLog by remember { mutableStateOf(false) }
            var basic by remember { mutableStateOf(readBasic()) }
            var benchmark by remember { mutableStateOf("") }
            var benchmarking by remember { mutableStateOf(false) }
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

            BtcrigTheme {
                BtcrigScreen(
                    ui = ui,
                    page = page,
                    benchmark = benchmark,
                    benchmarking = benchmarking,
                    onPage = { page = it },
                    onStart = {
                        if (startBtcrigService()) {
                            serviceState = "running"
                            ui = readUi().copy(running = true, service = "running")
                            refreshSoon()
                        } else {
                            page = 1
                            ui = readUi()
                        }
                    },
                    onStop = {
                        stopBtcrigService()
                        serviceState = "stopped"
                        ui = readUi().copy(running = false, service = "stopped")
                        refreshSoon()
                    },
                    onBenchmark = {
                        benchmarking = true
                        benchmark = "Benchmark: running"
                        val threads = Runtime.getRuntime().availableProcessors().coerceAtLeast(1)
                        Thread {
                            val hps = BtcrigNative.benchmarkCpu(2, threads)
                            runOnUiThread {
                                benchmarking = false
                                benchmark = "Benchmark: ${formatHashrate(hps)} / $threads threads"
                            }
                        }.start()
                    },
                    basic = basic,
                    onBasicChange = { basic = it },
                    onSaveBasic = {
                        runCatching { BtcrigConfig.writeBasic(this, it) }
                            .onSuccess {
                                basic = readBasic()
                                ui = readUi()
                                toast("Config saved.")
                            }
                            .onFailure { error -> toast("Save failed: ${error.message}") }
                    },
                    onJson = { showJson = true },
                    onLog = { showLog = true },
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
                                    toast("Config saved.")
                                }
                                .onFailure { error -> toast("Save failed: ${error.message}") }
                        },
                    )
                }

                if (showLog) {
                    TextDialog(
                        title = "btcrig.log",
                        text = readTail(File(filesDir, "btcrig.log"), 64 * 1024),
                        onDismiss = { showLog = false },
                    )
                }
            }
        }
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
            toast("Configure pool and user first.")
            return false
        }
        if (basic.cpuThreads <= 0 && !basic.openclEnabled) {
            toast("Enable CPU threads or OpenCL first.")
            return false
        }
        runCatching { BtcrigConfig.writeBasic(this, basic) }
            .onFailure { error ->
                toast("Config save failed: ${error.message}")
                return false
            }
        val intent = Intent(this, BtcrigService::class.java)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) startForegroundService(intent) else startService(intent)
        return true
    }

    private fun stopBtcrigService() {
        startService(Intent(this, BtcrigService::class.java).setAction(BtcrigService.ACTION_STOP))
    }

    private fun readUi(): UiState {
        val running = BtcrigNative.isRunning()
        val configPath = runCatching { BtcrigConfig.ensure(this).absolutePath }.getOrDefault("(unavailable)")
        val logPath = File(filesDir, "btcrig.log").absolutePath
        val configuredPool = runCatching { BtcrigConfig.readBasic(this).poolUrl.ifBlank { "not configured" } }
            .getOrDefault("unavailable")
        val configSummary = runCatching {
            val basic = BtcrigConfig.readBasic(this)
            "CPU: ${if (basic.cpuThreads > 0) "${basic.cpuThreads} threads" else "disabled"} / OpenCL: ${if (basic.openclEnabled) "enabled" else "disabled"}"
        }.getOrDefault("Config summary unavailable")
        val opencl = runCatching { BtcrigNative.openclStatus(configPath) }
            .getOrDefault("Config: unavailable\nRuntime: not probed\nMode: CPU only")

        return UiState(
            version = versionName(),
            backend = BtcrigNative.backendName(),
            selfTest = BtcrigNative.selfTest(),
            running = running,
            service = serviceState.ifEmpty { if (running) "running" else "stopped" },
            hashrate = if (running) formatHashrate(BtcrigNative.hashrate()) else "-- H/s",
            workers = if (running) "Workers: ${BtcrigNative.workerCount()}" else "Workers: --",
            total = if (running) "Total: ${BtcrigNative.totalHashes()}" else "Total: --",
            pool = if (running) BtcrigNative.pool().ifBlank { "(not configured)" } else configuredPool,
            stratum = if (running) {
                "Stratum: ${BtcrigNative.stratumStatus()} / connected: ${if (BtcrigNative.stratumConnected()) "yes" else "no"} / jobs: ${BtcrigNative.stratumJobs()}"
            } else {
                "Stratum: stopped"
            },
            shares = if (running) {
                "Shares: ${BtcrigNative.stratumSubmits()} submit / ${BtcrigNative.stratumAccepts()} ok / ${BtcrigNative.stratumRejects()} reject"
            } else {
                "Shares: --"
            },
            error = BtcrigNative.lastError().ifBlank { "none" },
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

@Composable
private fun BtcrigTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = lightColorScheme(
            primary = Color(0xFF4C6FFF),
            secondary = Color(0xFF667085),
            surface = Color.White,
            background = Color(0xFFF5F7FB),
            onPrimary = Color.White,
            onSurface = Color(0xFF18202E),
            onBackground = Color(0xFF18202E),
        ),
        content = content,
    )
}

@Composable
private fun BtcrigScreen(
    ui: UiState,
    page: Int,
    benchmark: String,
    benchmarking: Boolean,
    onPage: (Int) -> Unit,
    onStart: () -> Unit,
    onStop: () -> Unit,
    onBenchmark: () -> Unit,
    basic: BtcrigConfig.Basic,
    onBasicChange: (BtcrigConfig.Basic) -> Unit,
    onSaveBasic: (BtcrigConfig.Basic) -> Unit,
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
            Column(
                modifier = Modifier
                    .verticalScroll(rememberScrollState())
                    .padding(24.dp),
                verticalArrangement = Arrangement.spacedBy(16.dp),
            ) {
                Crossfade(targetState = page, animationSpec = tween(220), label = "page") { currentPage ->
                    Column(verticalArrangement = Arrangement.spacedBy(16.dp)) {
                        when (currentPage) {
                            1 -> {
                                Text("BTCRig", fontSize = 42.sp, fontWeight = FontWeight.Bold)
                                Text("Android miner shell · CPU / OpenCL", color = MaterialTheme.colorScheme.secondary)
                                SettingsPage(ui, basic, onBasicChange, onSaveBasic, onJson)
                            }
                            2 -> {
                                Text("BTCRig", fontSize = 42.sp, fontWeight = FontWeight.Bold)
                                Text("Android miner shell · CPU / OpenCL", color = MaterialTheme.colorScheme.secondary)
                                InfoPage(ui, benchmark, benchmarking, onBenchmark, onLog)
                            }
                            else -> HomePage(ui, onStart, onStop)
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun BottomNav(page: Int, onPage: (Int) -> Unit) {
    Surface(color = MaterialTheme.colorScheme.surface) {
        Row(modifier = Modifier.fillMaxWidth().padding(horizontal = 24.dp, vertical = 12.dp)) {
            listOf("首页", "设置", "信息").forEachIndexed { index, title ->
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
                            .background(if (page == index) Color(0xFF0B2D5F) else Color.Transparent),
                    )
                    Text(
                        title,
                        color = if (page == index) Color(0xFF0B2D5F) else MaterialTheme.colorScheme.secondary,
                        fontWeight = if (page == index) FontWeight.Bold else FontWeight.Normal,
                    )
                }
            }
        }
    }
}

@Composable
private fun HomePage(
    ui: UiState,
    onStart: () -> Unit,
    onStop: () -> Unit,
) {
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
                Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(18.dp)) {
                    Box(modifier = Modifier.height(74.dp))
                    HashrateText(ui.hashrate)
                    Text("BTCRig v${ui.version}", color = MaterialTheme.colorScheme.secondary)
                }
            }
        }
        EnterUp(delayMillis = 180) {
            StatusPill(
                running = ui.running,
                text = if (ui.running) "运行中" else "已停止",
                onClick = { if (ui.running) onStop() else onStart() },
            )
        }
        EnterUp(delayMillis = 320) {
            Column(modifier = Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                SpecRow("CPU", ui.cpuSummary)
                SpecRow("GPU", ui.gpuSummary)
                SpecRow("矿池", ui.pool)
            }
        }
        if (ui.error != "none") {
            AppCard("Last error") { Line(ui.error) }
        }
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
        Text(value, fontSize = 56.sp, fontWeight = FontWeight.Bold)
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
                        if (running) Color(0x3318A058) else Color(0x334C6FFF),
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
    val color = if (running) Color(0xFF18A058) else Color(0xFF0B2D5F)
    Row(
        modifier = Modifier
            .clip(RoundedCornerShape(999.dp))
            .background(if (running) Color(0xFFDFF8E9) else Color(0xFFE5ECFF))
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
    onSaveBasic: (BtcrigConfig.Basic) -> Unit,
    onJson: () -> Unit,
) {
    val enabled = !ui.running
    AppCard("设置") {
        SettingField(
            value = basic.poolUrl,
            onValueChange = { onBasicChange(basic.copyBasic(poolUrl = it)) },
            label = "矿池地址",
            enabled = enabled,
        )
        SettingField(
            value = basic.user,
            onValueChange = { onBasicChange(basic.copyBasic(user = it)) },
            label = "用户 / worker",
            enabled = enabled,
        )
        SettingField(
            value = basic.pass,
            onValueChange = { onBasicChange(basic.copyBasic(pass = it)) },
            label = "密码",
            enabled = enabled,
        )
        SettingField(
            value = basic.cpuThreads.toString(),
            onValueChange = { onBasicChange(basic.copyBasic(cpuThreads = it.filter(Char::isDigit).toIntOrNull() ?: 0)) },
            label = "CPU 数量",
            enabled = enabled,
            helper = "0 = 不使用 CPU 挖矿",
        )
        SettingSwitchRow("启用 OpenCL / GPU", basic.openclEnabled, enabled) {
            onBasicChange(basic.copyBasic(openclEnabled = it))
        }
        if (!enabled) {
            Line("停止服务后才能保存设置")
        }
        Button(onClick = { onSaveBasic(basic) }, enabled = enabled, modifier = Modifier.fillMaxWidth()) {
            Text("保存设置")
        }
        OutlinedButton(onClick = onJson, enabled = enabled, modifier = Modifier.fillMaxWidth()) {
            Text("高级 JSON")
        }
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
    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
        TextField(
            value = value,
            onValueChange = onValueChange,
            label = { Text(label) },
            singleLine = true,
            enabled = enabled,
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(18.dp),
            colors = TextFieldDefaults.colors(
                focusedContainerColor = Color(0xFFF6F7FC),
                unfocusedContainerColor = Color(0xFFF6F7FC),
                disabledContainerColor = Color(0xFFF6F7FC),
                focusedIndicatorColor = Color.Transparent,
                unfocusedIndicatorColor = Color.Transparent,
                disabledIndicatorColor = Color.Transparent,
            ),
        )
        if (helper.isNotBlank()) {
            Text(
                helper,
                modifier = Modifier.padding(start = 16.dp),
                color = MaterialTheme.colorScheme.secondary,
                fontSize = 12.sp,
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
            .clip(RoundedCornerShape(18.dp))
            .background(Color(0xFFF6F7FC))
            .clickable(enabled = enabled) { onCheckedChange(!checked) }
            .padding(horizontal = 18.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text(title, modifier = Modifier.weight(1f), fontSize = 16.sp, lineHeight = 22.sp)
        Switch(checked = checked, onCheckedChange = if (enabled) onCheckedChange else null, enabled = enabled)
    }
}

@Composable
private fun InfoPage(
    ui: UiState,
    benchmark: String,
    benchmarking: Boolean,
    onBenchmark: () -> Unit,
    onLog: () -> Unit,
) {
    OutlinedButton(onClick = onBenchmark, enabled = !benchmarking, modifier = Modifier.fillMaxWidth()) {
        Text("CPU benchmark")
    }
    if (benchmark.isNotBlank()) {
        AppCard("Benchmark") { Line(benchmark) }
    }
    OutlinedButton(onClick = onLog, modifier = Modifier.fillMaxWidth()) { Text("View log") }
    AppCard("Info") {
        Line("Backend: ${ui.backend}")
        Line("Self-test: ${if (ui.selfTest) "ok" else "failed"}")
        Line("OpenCL:\n${ui.opencl}")
        Line("Config: ${ui.configPath}\nLog: ${ui.logPath}")
    }
}

@Composable
private fun AppCard(title: String, content: @Composable ColumnScope.() -> Unit) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = Color(0xFFF0F2F8)),
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
    Text(text, color = MaterialTheme.colorScheme.secondary, lineHeight = 22.sp)
}

@Composable
private fun JsonDialog(text: String, onDismiss: () -> Unit, onSave: (String) -> Unit) {
    var json by remember { mutableStateOf(text) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Edit config.json") },
        text = {
            OutlinedTextField(
                value = json,
                onValueChange = { json = it },
                modifier = Modifier
                    .fillMaxWidth()
                    .height(360.dp),
            )
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
        confirmButton = { Button(onClick = { onSave(json) }) { Text("Save") } },
    )
}

@Composable
private fun TextDialog(title: String, text: String, onDismiss: () -> Unit) {
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
        confirmButton = { Button(onClick = onDismiss) { Text("Close") } },
    )
}

private fun readTail(file: File, maxBytes: Int): String {
    if (!file.exists()) return "(log not found)"
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
            if (n > 0) String(buffer, 0, n) else "(empty log)"
        }
    }.getOrElse { "Log read failed: ${it.message}" }
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

private fun BtcrigConfig.Basic.copyBasic(
    poolUrl: String = this.poolUrl,
    user: String = this.user,
    pass: String = this.pass,
    cpuThreads: Int = this.cpuThreads,
    openclEnabled: Boolean = this.openclEnabled,
): BtcrigConfig.Basic {
    val next = BtcrigConfig.Basic()
    next.poolUrl = poolUrl
    next.user = user
    next.pass = pass
    next.cpuThreads = cpuThreads.coerceAtLeast(0)
    next.openclEnabled = openclEnabled
    return next
}

private fun formatHashrate(hps: Double): String = when {
    hps >= 1_000_000_000.0 -> String.format(Locale.US, "%.2f GH/s", hps / 1_000_000_000.0)
    hps >= 1_000_000.0 -> String.format(Locale.US, "%.2f MH/s", hps / 1_000_000.0)
    hps >= 1_000.0 -> String.format(Locale.US, "%.2f KH/s", hps / 1_000.0)
    else -> String.format(Locale.US, "%.0f H/s", hps)
}
