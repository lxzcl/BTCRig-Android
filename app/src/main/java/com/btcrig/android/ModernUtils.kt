package com.btcrig.android

import java.io.File
import java.io.FileInputStream
import java.net.HttpURLConnection
import java.net.URL
import java.util.Locale
import org.json.JSONObject

internal fun readTail(
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

internal fun cleanLog(text: String): String = AnsiEscape.replace(text, "")

internal fun recentLogError(file: File): String {
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

internal fun fetchLatestRelease(): ReleaseInfo {
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

internal fun updateStateFor(
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

internal fun previewUi() = UiState(
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

internal fun previewBasic() = BtcrigConfig.Basic().apply {
    poolUrl = "stratum+tcp://public-pool.io:3333"
    user = "bc1qqz0wutk9kk5mmaf7fu4dm5w4fq4fhaah9hpzr3"
    pass = "x"
    cpuThreads = 8
    openclEnabled = true
    certCompat = true
    wakeLock = true
    donationPercent = 1
}

internal fun BtcrigConfig.Basic.copyBasic(
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

internal fun ByteArray.toHex(): String = joinToString("") { "%02x".format(it.toInt() and 0xff) }

internal fun formatHashrate(hps: Double): String = when {
    hps >= 1_000_000_000.0 -> String.format(Locale.US, "%.2f GH/s", hps / 1_000_000_000.0)
    hps >= 1_000_000.0 -> String.format(Locale.US, "%.2f MH/s", hps / 1_000_000.0)
    hps >= 1_000.0 -> String.format(Locale.US, "%.2f KH/s", hps / 1_000.0)
    else -> String.format(Locale.US, "%.0f H/s", hps)
}
