package com.btcrig.android

internal data class UiState(
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

internal data class UpdateState(
    val latestVersion: String = "",
    val url: String = "",
    val available: Boolean = false,
    val checking: Boolean = false,
    val error: String = "",
)

internal data class ReleaseInfo(val version: String, val url: String)

internal data class OpenclInfo(val name: String, val version: String)

internal data class RankUi(
    val title: String,
    val message: String = "",
    val rows: List<RankUiRow> = emptyList(),
    val me: RankUiRow? = null,
)

internal data class RankUiRow(val rank: Int, val name: String, val rate: String, val detail: String = "")

