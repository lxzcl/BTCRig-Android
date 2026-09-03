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

internal data class BenchmarkProof(
    val hps: Double = -1.0,
    val nonce: Long = 0,
    val hash: String = "",
    val found: Boolean = false,
)

internal data class BenchmarkResult(
    val cpuHps: Double,
    val gpuHps: Double,
    val cpuGpuHps: Double,
    val cpuProof: BenchmarkProof = BenchmarkProof(),
    val gpuProof: BenchmarkProof = BenchmarkProof(),
    val cpuGpuProof: BenchmarkProof = BenchmarkProof(),
)

internal data class SubmitResult(val accepted: Boolean, val text: String)

internal data class BenchmarkChallenge(
    val id: String,
    val seed: String,
    val token: String,
    val seconds: Int,
    val proofDifficulty: Double,
)

internal data class RankUi(
    val title: String,
    val message: String = "",
    val rows: List<RankUiRow> = emptyList(),
    val me: RankUiRow? = null,
)

internal data class RankUiRow(val rank: Int, val name: String, val rate: String, val detail: String = "")
