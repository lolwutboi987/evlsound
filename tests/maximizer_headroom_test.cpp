// Offline synthetic stress test: real flat-EQ/master processing followed by
// the real maximizer. No DfxDsp construction, audio device, or settings access.
// The 5.0-peak float input is deliberately above full scale to exercise limiting;
// it does not represent a measurement of the user's EQ or music.
#include "GraphicEq.h"
#include "qnt.h"
#include "pt_defs.h"
#include "c_max.h"
extern "C" {
#include "c_dsps.h"
}
#include <cmath>
#include <cstdio>
#include <cstdlib>

static void require(bool ok, const char* message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

struct Result { int limited_frames = 0; float peak = 0; };

static Result run(float gain_db, float requested_boost)
{
    PT_HANDLE* eq = nullptr;
    require(GraphicEqNew(&eq, 10, 0, nullptr) == OKAY, "create flat EQ");
    GraphicEqSetMasterGain(eq, gain_db);
    dspMaxiStructType maxi = {};
    float state[DSPS_NUM_STATE_VARS] = {};
    float memory = 0;
    auto params = reinterpret_cast<float*>(&maxi);
    require(dspsMaximizerInit(params, &memory, 0, state,
        DSPS_INIT_PARAMS | DSPS_INIT_MEMORY, 44100.0f) == OKAY, "initialize maximizer");
    // Play32.c sets wet=1/dry=0 after generic initialization. The other
    // initialized fixed values (including 0.966051 ceiling) are unchanged.
    maxi.stereo_in_flag = 1;
    maxi.wet_gain = 1.0f;
    maxi.dry_gain = 0.0f;
    maxi.gain_boost = requested_boost;
    hardwareMeterValType meters = {};
    Result result;
    for (int frame = 0; frame < 44100 * 4; ++frame)
    {
        float sample = 5.0f * std::sin(6.283185307f * 1000.0f * frame / 44100.0f);
        realtype input[2] = { sample, sample };
        realtype output[2] = {};
        require(GraphicEqProcess(eq, input, output, 1, 2, 44100.0f) == OKAY, "process EQ/master");
        dspsMaximizerProcess32(reinterpret_cast<long*>(output), 1, params, &memory, state, &meters, 0);
        require(std::isfinite(output[0]) && std::fabs(output[0]) <= maxi.max_output + 0.00001f,
            "finite output below unchanged ceiling");
        if (maxi.env_l > maxi.max_output) ++result.limited_frames;
        if (std::fabs(output[0]) > result.peak) result.peak = std::fabs(output[0]);
    }
    require(GraphicEqFreeUp(&eq) == OKAY, "free EQ");
    std::printf("master=%+.0f dB, peak=%.6f, limited_frames=%d/176400\n",
        gain_db, result.peak, result.limited_frames);
    return result;
}

int main()
{
    // Same quantizer and scaled MIDI maximum as dfxpQnt.cpp/dfxpComm.cpp:
    // Dynamic Boost max is truncated 127 * 0.7 = 88, not the 30 dB endpoint.
    PT_HANDLE* quant = nullptr;
    require(qntIToRInit(&quant, nullptr, 0, 127, 0.0f, 30.0f,
        0, 0, 0, 0.0f, 0, QNT_RESPONSE_MAXI_BOOST_DSP) == OKAY, "create boost quantizer");
    realtype boost = 0;
    require(qntIToRCalc(quant, static_cast<int>(127 * 0.7f), &boost) == OKAY, "read max Dynamic Boost");
    require(qntFreeUp(&quant) == OKAY, "free quantizer");
    std::printf("Dynamic Boost cap=%.6f (+%.3f dB), ceiling=0.966051\n", boost, 20.0f * std::log10(boost));
    const auto a = run(-20.0f, boost);
    const auto b = run(-40.0f, boost);
    const auto c = run(-60.0f, boost);
    require(a.limited_frames > 0, "stress signal exercises limiter at -20 dB");
    require(b.limited_frames == 0 && c.limited_frames == 0, "deeper master gain prevents limiting for this signal");
    require(c.peak < b.peak && b.peak < a.peak, "deeper master gain reduces output despite Dynamic Boost");
    std::puts("PASS: synthetic maximizer headroom stress comparison");
}
