package com.btcrig.android

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
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
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
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
import androidx.compose.ui.tooling.preview.Preview
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
        enableEdgeToEdge()
        requestNotificationPermission()

        setContent {
            var ui by remember { mutableStateOf(readUi()) }
            var page by remember { mutableStateOf(0) }
            var showJson by remember { mutableStateOf(false) }
            var showLog by remember { mutableStateOf(false) }
            var basic by remember { mutableStateOf(readBasic()) }
            var benchmark by remember { mutableStateOf(defaultBenchmarkText()) }
            var benchmarking by remember { mutableStateOf(false) }
            fun saveBasic(next: BtcrigConfig.Basic) {
                basic = next
                runCatching { BtcrigConfig.writeBasic(this, next) }
                    .onSuccess { ui = readUi() }
                    .onFailure { error -> toast("Save failed: ${error.message}") }
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
                        if (ui.running) {
                            toast("Stop mining before benchmark.")
                            return@BtcrigScreen
                        }
                        benchmarking = true
                        val configPath = runCatching { BtcrigConfig.ensure(this).absolutePath }.getOrDefault("")
                        val threads = Runtime.getRuntime().availableProcessors().coerceAtLeast(1)
                        Thread {
                            val lines = mutableListOf("Benchmark", "CPU full cores: $threads", "")
                            for (backend in CPU_BACKENDS) {
                                runOnUiThread {
                                    benchmark = (lines + "$backend: testing...").joinToString("\n")
                                }
                                val hps = BtcrigNative.benchmarkCpuBackend(backend, 1, threads)
                                lines.add("$backend: ${if (hps >= 0.0) formatHashrate(hps) else "unavailable"}")
                            }
                            runOnUiThread {
                                benchmark = (lines + "opencl: testing...").joinToString("\n")
                            }
                            val openclHps = BtcrigNative.benchmarkOpencl(configPath, 1)
                            lines.add("opencl: ${if (openclHps >= 0.0) formatHashrate(openclHps) else "unavailable"}")
                            runOnUiThread {
                                benchmarking = false
                                benchmark = lines.joinToString("\n")
                            }
                        }.start()
                    },
                    basic = basic,
                    onBasicChange = { saveBasic(it) },
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

private val Ink = Color(0xFF172033)
private val Muted = Color(0xFF6E7890)
private val Accent = Color(0xFF26364F)
private val RigBlue = Color(0xFF4C6F9F)
private val SoftBlue = Color(0xFFE8EDF8)
private val CardFill = Color(0xFFEFF1F7)
private val FieldFill = Color.Transparent
private val CPU_BACKENDS = listOf("openssl", "fast-c", "arm-sha2", "x86-sha-ni")
private val DONATION_LEVELS = listOf(0, 1, 3, 5, 99)

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

@Composable
private fun PreviewScreen(page: Int) {
    BtcrigTheme {
        BtcrigScreen(
            ui = previewUi(),
            page = page,
            benchmark = defaultBenchmarkText(),
            benchmarking = false,
            onPage = {},
            onStart = {},
            onStop = {},
            onBenchmark = {},
            basic = previewBasic(),
            onBasicChange = {},
            onJson = {},
            onLog = {},
        )
    }
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
                                SettingsPage(ui, basic, onBasicChange, onJson)
                            }
                        }
                        2 -> {
                            Column(
                                modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()),
                                verticalArrangement = Arrangement.spacedBy(16.dp),
                            ) {
                                PageHeader()
                                InfoPage(ui, benchmark, benchmarking, onBenchmark, onLog, basic, onBasicChange)
                            }
                        }
                        else -> HomePage(
                            ui = ui,
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
                    Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(18.dp)) {
                        Box(modifier = Modifier.height(74.dp))
                        HashrateText(ui.hashrate)
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
        Box(modifier = Modifier.align(Alignment.BottomCenter).fillMaxWidth()) {
            EnterUp(delayMillis = 420) {
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(bottom = 8.dp),
                    contentAlignment = Alignment.BottomCenter,
                ) {
                    BrandHeader(ui.version)
                }
            }
        }
    }
}

@Composable
private fun PageHeader() {
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        BrandTitle(42)
        Text("Android miner shell · CPU / OpenCL", color = MaterialTheme.colorScheme.secondary)
    }
}

@Composable
private fun BrandHeader(version: String) {
    Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(
                "↗",
                modifier = Modifier.graphicsLayer { alpha = 0f },
                color = Accent,
                fontSize = 11.sp,
                fontWeight = FontWeight.Bold,
            )
            Text("v$version", color = MaterialTheme.colorScheme.secondary, fontSize = 13.sp)
            UpdateBadge()
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
private fun UpdateBadge() {
    Text(
        "↗",
        color = Accent,
        fontSize = 11.sp,
        fontWeight = FontWeight.Bold,
    )
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
    onJson: () -> Unit,
) {
    val enabled = !ui.running
    SettingSection("设置", compact = true) {
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
        SettingSwitchRow("兼容未知证书", basic.certCompat, enabled) {
            onBasicChange(basic.copyBasic(certCompat = it))
        }
        if (!enabled) {
            Line("停止服务后才能保存设置")
        }
    }
    RigButton(text = "高级 JSON", onClick = onJson, enabled = enabled)
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
    benchmark: String,
    benchmarking: Boolean,
    onBenchmark: () -> Unit,
    onLog: () -> Unit,
    basic: BtcrigConfig.Basic,
    onBasicChange: (BtcrigConfig.Basic) -> Unit,
) {
    Text("信息", color = RigBlue, fontSize = 13.sp, fontWeight = FontWeight.Medium)
    RigButton(
        text = if (benchmarking) "Benchmarking..." else "Benchmark CPU",
        onClick = onBenchmark,
        enabled = !benchmarking && !ui.running
    )
    BenchmarkBox(benchmark)
    RigButton(text = "View log", onClick = onLog)
    SoftCard(compact = true) {
        Line("Backend: ${ui.backend}")
        Line("Self-test: ${if (ui.selfTest) "ok" else "failed"}")
        Line("OpenCL:\n${ui.opencl}")
        Line("Config: ${ui.configPath}\nLog: ${ui.logPath}")
    }
    DonationCard(
        percent = basic.donationPercent,
        enabled = !ui.running,
        onChange = { onBasicChange(basic.copyBasic(donationPercent = it)) },
    )
}

@Composable
private fun DonationCard(percent: Int, enabled: Boolean, onChange: (Int) -> Unit) {
    val index = DONATION_LEVELS.indexOf(percent).takeIf { it >= 0 } ?: DONATION_LEVELS.indexOf(1)
    SettingSection("支持作者", compact = true) {
        Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text("捐赠比例", modifier = Modifier.weight(1f), color = MaterialTheme.colorScheme.secondary, fontSize = 15.sp)
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
                .height(150.dp)
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
    error = "none",
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
    donationPercent = 1
}

private fun BtcrigConfig.Basic.copyBasic(
    poolUrl: String = this.poolUrl,
    user: String = this.user,
    pass: String = this.pass,
    cpuThreads: Int = this.cpuThreads,
    openclEnabled: Boolean = this.openclEnabled,
    certCompat: Boolean = this.certCompat,
    donationPercent: Int = this.donationPercent,
): BtcrigConfig.Basic {
    val next = BtcrigConfig.Basic()
    next.poolUrl = poolUrl
    next.user = user
    next.pass = pass
    next.cpuThreads = cpuThreads.coerceAtLeast(0)
    next.openclEnabled = openclEnabled
    next.certCompat = certCompat
    next.donationPercent = DONATION_LEVELS.find { it == donationPercent } ?: 1
    return next
}

private fun defaultBenchmarkText(): String =
    (listOf("Benchmark", "CPU full cores: --", "") + CPU_BACKENDS.map { "$it: --" } + "opencl: --").joinToString("\n")

private fun formatHashrate(hps: Double): String = when {
    hps >= 1_000_000_000.0 -> String.format(Locale.US, "%.2f GH/s", hps / 1_000_000_000.0)
    hps >= 1_000_000.0 -> String.format(Locale.US, "%.2f MH/s", hps / 1_000_000.0)
    hps >= 1_000.0 -> String.format(Locale.US, "%.2f KH/s", hps / 1_000.0)
    else -> String.format(Locale.US, "%.0f H/s", hps)
}
