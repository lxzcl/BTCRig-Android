package com.btcrig.android

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
import androidx.compose.foundation.layout.Spacer
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
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import java.util.Locale

private val Ink = Color(0xFF172033)
private val Muted = Color(0xFF6E7890)
private val Accent = Color(0xFF26364F)
private val RigBlue = Color(0xFF4C6F9F)
private val SoftBlue = Color(0xFFE8EDF8)
private val CardFill = Color(0xFFEFF1F7)
private val FieldFill = Color.Transparent
internal const val BENCHMARK_SECONDS = 3
internal const val UPLOAD_BENCHMARK_SECONDS = 10
internal val DONATION_LEVELS = listOf(0, 1, 3, 5, 99)
internal const val RANK_API_BASE_URL = "https://www.btcrig.net/api/v1"
internal const val UPDATE_API_URL = "https://api.github.com/repos/lxzcl/BTCRig-Android/releases/latest"
internal const val UPDATE_RELEASE_URL = "https://github.com/lxzcl/BTCRig-Android/releases/latest"
internal const val UPDATE_CACHE_MS = 6 * 60 * 60 * 1000L

@Composable
internal fun BtcrigTheme(content: @Composable () -> Unit) {
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
            leaderboard = RankUi(
                "all",
                rows = listOf(RankUiRow(1, "QTI SM8650 · Adreno(TM) 750", "141.30 MH/s")),
                me = RankUiRow(12, "QTI SM8650 · Adreno(TM) 750", "141.30 MH/s"),
            ),
            benchmarking = false,
            uploadingBenchmark = false,
            onPage = {},
            onRankMode = {},
            onOpenUpdate = {},
            onStart = {},
            onStop = {},
            onBenchmark = {},
            onUploadBenchmark = {},
            basic = previewBasic(),
            onBasicChange = {},
            onBatteryOptimization = {},
            onJson = {},
            onLog = {},
        )
    }
}

@Composable
internal fun BtcrigScreen(
    ui: UiState,
    update: UpdateState,
    page: Int,
    benchmark: String,
    rankMode: String,
    leaderboard: RankUi,
    benchmarking: Boolean,
    uploadingBenchmark: Boolean,
    onPage: (Int) -> Unit,
    onRankMode: (String) -> Unit,
    onOpenUpdate: () -> Unit,
    onStart: () -> Unit,
    onStop: () -> Unit,
    onBenchmark: () -> Unit,
    onUploadBenchmark: () -> Unit,
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
                                InfoPage(ui, update, benchmark, benchmarking, uploadingBenchmark, onBenchmark, onUploadBenchmark, onLog, basic, onBasicChange)
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
    uploadingBenchmark: Boolean,
    onBenchmark: () -> Unit,
    onUploadBenchmark: () -> Unit,
    onLog: () -> Unit,
    basic: BtcrigConfig.Basic,
    onBasicChange: (BtcrigConfig.Basic) -> Unit,
) {
    Text(stringResource(R.string.info_title), color = RigBlue, fontSize = 13.sp, fontWeight = FontWeight.Medium)
    Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
        RigButton(
            text = if (benchmarking) stringResource(R.string.benchmarking) else stringResource(R.string.benchmark),
            onClick = onBenchmark,
            enabled = !benchmarking && !uploadingBenchmark && !ui.running
        )
        RigButton(
            text = if (uploadingBenchmark) stringResource(R.string.uploading_score) else stringResource(R.string.upload_score),
            onClick = onUploadBenchmark,
            enabled = !benchmarking && !uploadingBenchmark && !ui.running
        )
    }
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
    Spacer(Modifier.height(16.dp))
}

@Composable
private fun ColumnScope.RankPage(
    leaderboard: RankUi,
    rankMode: String,
    onRankMode: (String) -> Unit,
) {
    var selected by remember { mutableStateOf<RankUiRow?>(null) }
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
    Box(Modifier.weight(1f).padding(bottom = 16.dp)) {
        RankBox(
            leaderboard,
            Modifier.fillMaxSize(),
            bottomContentPadding = if (leaderboard.me == null) 0.dp else 82.dp,
            onRowClick = { selected = it },
        )
        leaderboard.me?.let {
            MyRankCard(
                it,
                Modifier.align(Alignment.BottomCenter),
                onClick = { selected = it },
            )
        }
    }
    selected?.let { row ->
        AlertDialog(
            onDismissRequest = { selected = null },
            title = { Text(row.name, color = Ink, fontSize = 20.sp, fontWeight = FontWeight.Medium) },
            text = {
                Text(
                    row.detail,
                    modifier = Modifier.verticalScroll(rememberScrollState()),
                    color = MaterialTheme.colorScheme.secondary,
                    fontSize = 13.sp,
                    lineHeight = 19.sp,
                )
            },
            confirmButton = {
                TextButton(onClick = { selected = null }) {
                    Text(stringResource(R.string.close))
                }
            },
        )
    }
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
private fun RankBox(
    rank: RankUi,
    modifier: Modifier = Modifier,
    bottomContentPadding: Dp = 0.dp,
    onRowClick: (RankUiRow) -> Unit = {},
) {
    Card(
        modifier = modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = CardFill),
        elevation = CardDefaults.cardElevation(defaultElevation = 0.dp),
        shape = RoundedCornerShape(22.dp),
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(start = 14.dp, top = 14.dp, end = 14.dp, bottom = 14.dp + bottomContentPadding)
                .verticalScroll(rememberScrollState()),
        ) {
            if (rank.message.isNotBlank()) {
                Text(rank.message, color = MaterialTheme.colorScheme.secondary, fontSize = 14.sp, lineHeight = 20.sp)
            }
            rank.rows.forEachIndexed { index, row ->
                RankLine(row, onClick = { onRowClick(row) })
                if (index != rank.rows.lastIndex) {
                    Box(
                        Modifier
                            .fillMaxWidth()
                            .height(10.dp)
                            .drawBehind {
                                drawLine(
                                    Color(0x14000000),
                                    Offset(0f, size.height / 2f),
                                    Offset(size.width, size.height / 2f),
                                    strokeWidth = 1.dp.toPx(),
                                )
                            }
                    )
                }
            }
        }
    }
}

@Composable
private fun RankLine(row: RankUiRow, onClick: () -> Unit = {}) {
    Row(
        modifier = Modifier.fillMaxWidth().clickable(onClick = onClick),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            "${rankPrefix(row.rank)} ${row.name}",
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

private fun rankPrefix(rank: Int): String = when (rank) {
    1 -> "🥇"
    2 -> "🥈"
    3 -> "🥉"
    else -> "#$rank"
}

@Composable
private fun MyRankCard(row: RankUiRow, modifier: Modifier = Modifier, onClick: () -> Unit = {}) {
    Card(
        modifier = modifier.fillMaxWidth().clickable(onClick = onClick),
        colors = CardDefaults.cardColors(containerColor = Color.White),
        elevation = CardDefaults.cardElevation(defaultElevation = 0.dp),
        shape = RoundedCornerShape(20.dp),
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(stringResource(R.string.my_rank), color = RigBlue, fontSize = 12.sp, fontWeight = FontWeight.Medium)
                Text(
                    "${rankPrefix(row.rank)} ${row.name}",
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    color = MaterialTheme.colorScheme.secondary,
                    fontSize = 13.sp,
                )
            }
            Text(row.rate, color = Ink, fontSize = 13.sp, fontWeight = FontWeight.Bold, maxLines = 1)
        }
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
    val scroll = rememberScrollState()
    LaunchedEffect(text, scroll.maxValue) {
        scroll.scrollTo(scroll.maxValue)
    }
    SoftCard(compact = true) {
        Text(
            text,
            modifier = Modifier
                .fillMaxWidth()
                .height(144.dp)
                .verticalScroll(scroll),
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
internal fun JsonDialog(text: String, onDismiss: () -> Unit, onSave: (String) -> Unit) {
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
internal fun TextDialog(title: String, text: String, onCopy: () -> Unit, onDismiss: () -> Unit) {
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
