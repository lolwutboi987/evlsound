// Offline regression test: links the real DSP library and never opens an
// audio device, starts FxSound, or reads/writes the user's settings.
#include "GraphicEq.h"
#include "DfxMasterGain.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>

namespace
{
	void require(bool condition, const char* message)
	{
		if (!condition)
		{
			std::fprintf(stderr, "FAIL: %s\n", message);
			std::exit(EXIT_FAILURE);
		}
	}

	void checkGain(PT_HANDLE* eq, float requested_db, float expected_db)
	{
		GraphicEqSetMasterGain(eq, requested_db);
		float actual_db = 999.0f;
		require(GraphicEqGetMasterGain(eq, &actual_db) == OKAY, "read master gain");
		require(actual_db == expected_db, "stored master gain matches the validated request");

		const float multiplier = std::pow(10.0f, expected_db / 20.0f);
		for (int channels : { 1, 2, 6, 8 })
		{
			const int frames = 4;
			realtype input[frames * 8] = {};
			realtype output[frames * 8] = {};
			for (int i = 0; i < frames * channels; ++i)
				input[i] = (i % 2 == 0 ? 0.0125f : -0.025f);

			require(GraphicEqProcess_MasterGainOnly(eq, input, output, frames, channels, 44100.0f) == OKAY,
				"process gain-only samples");
			for (int i = 0; i < frames * channels; ++i)
			{
				const float expected = input[i] * multiplier;
				require(std::isfinite(output[i]) &&
					std::fabs(output[i] - expected) <= std::fabs(expected) * 0.00002f + 1.0e-10f,
					"gain-only sample amplitude and polarity");
			}

			// With the default flat EQ, the normal EQ path must apply the same
			// master gain. This also covers mono, stereo, and surround routing.
			require(GraphicEqProcess(eq, input, output, frames, channels, 44100.0f) == OKAY,
				"process flat-EQ samples");
			for (int i = 0; i < frames * channels; ++i)
			{
				const float expected = input[i] * multiplier;
				require(std::isfinite(output[i]) &&
					std::fabs(output[i] - expected) <= std::fabs(expected) * 0.00002f + 1.0e-10f,
					"flat-EQ sample amplitude and polarity");
			}
		}
	}
}

int main()
{
	static_assert(DfxMasterGain::kMinDb == -60.0f, "headroom lower bound");
	static_assert(DfxMasterGain::kMaxDb == 20.0f, "unchanged upper bound");
	static_assert(DfxMasterGain::kDefaultDb == 0.0f, "unchanged default");

	struct ParsedGainCase
	{
		const char* text;
		float expected_db;
	};
	for (const auto& value : { ParsedGainCase{ "-60", -60.0f }, { "-40.5", -40.5f },
		{ "0", 0.0f }, { "+20", 20.0f }, { "-6e1", -60.0f }, { "2E+1", 20.0f },
		{ "-.5", -0.5f }, { "20.", 20.0f } })
	{
		float gain_db = -40.0f;
		require(DfxMasterGain::tryParse(value.text, gain_db), "accept complete in-range numeric input");
		require(gain_db == value.expected_db, "parsed gain matches numeric input");
	}

	PT_HANDLE* eq = nullptr;
	require(GraphicEqNew(&eq, 10, 0, nullptr) == OKAY, "create flat equalizer");
	float initial_gain = 999.0f;
	require(GraphicEqGetMasterGain(eq, &initial_gain) == OKAY && initial_gain == 0.0f,
		"initial gain remains zero dB");

	for (float gain_db : { -60.0f, -59.0f, -40.5f, -40.0f, -20.0f, 0.0f, 20.0f })
		checkGain(eq, gain_db, gain_db);

	// Bad input must never remove the caller's existing attenuation.
	GraphicEqSetMasterGain(eq, -40.0f);
	for (float invalid_db : { -60.01f, 20.01f,
		std::numeric_limits<float>::quiet_NaN(),
		std::numeric_limits<float>::infinity(),
		-std::numeric_limits<float>::infinity() })
		checkGain(eq, invalid_db, -40.0f);

	for (const char* invalid_text : { "", "oops", "- 40", "-40oops", "-40 dB",
		" -40", "-40 ", "--40", "+", ".", "-40,5", "0x10", "nan", "NaN",
		"inf", "-inf", "1e999", "-100", "21", "-60.4", "20.4",
		"-60.000001", "20.0000001", "-4e" })
	{
		float live_gain = -40.0f;
		const bool accepted = DfxMasterGain::tryParse(invalid_text, live_gain);
		if (accepted)
			std::fprintf(stderr, "Unexpectedly accepted '%s' as %.9g dB\n", invalid_text, live_gain);
		require(!accepted, "reject invalid CLI input");
		require(live_gain == -40.0f, "invalid CLI text preserves current attenuation");
		checkGain(eq, DfxMasterGain::startupGain("-40", invalid_text), -40.0f);
	}

	float unchanged_gain = -40.0f;
	require(!DfxMasterGain::tryParse(nullptr, unchanged_gain) && unchanged_gain == -40.0f,
		"missing input preserves current attenuation");
	require(DfxMasterGain::startupGain("-40", "-60") == -60.0f,
		"valid CLI override takes precedence over saved gain");
	require(DfxMasterGain::startupGain("-40", "0") == 0.0f,
		"explicit zero-dB override remains supported");
	require(DfxMasterGain::startupGain("oops", "-40") == -40.0f,
		"valid CLI override replaces invalid saved gain");
	require(DfxMasterGain::startupGain("oops", "-100") == 0.0f,
		"invalid saved value and invalid override retain initial default");
	require(DfxMasterGain::startupGain(nullptr, nullptr) == 0.0f,
		"missing saved value and missing override retain initial default");

	GraphicEqSetMasterGain(nullptr, -60.0f);
	require(GraphicEqFreeUp(&eq) == OKAY && eq == nullptr, "free equalizer");
	std::puts("PASS: strict master gain parsing, startup precedence, and real DSP samples (mono/stereo/5.1/7.1)");
	return EXIT_SUCCESS;
}
