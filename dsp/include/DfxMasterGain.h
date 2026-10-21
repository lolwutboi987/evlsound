/*
FxSound
Copyright (C) 2025 FxSound LLC

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
#pragma once

#include <locale>
#include <sstream>

namespace DfxMasterGain
{
	constexpr float kMinDb = -60.0f;
	constexpr float kMaxDb = 20.0f;
	constexpr float kDefaultDb = 0.0f;

	// The positive range check also rejects NaN and infinities.
	constexpr bool isValid(float gain_db)
	{
		return gain_db >= kMinDb && gain_db <= kMaxDb;
	}

	// Parse the entire value as a decimal number using '.' in every locale.
	// Reject whitespace, suffixes, NaN/infinity, and out-of-range input before
	// conversion to float or slider rounding can conceal an invalid value.
	// On failure the caller's current gain is left untouched.
	inline bool tryParse(const char* text, float& gain_db)
	{
		if (text == nullptr || *text == '\0')
			return false;

		for (const char* ch = text; *ch != '\0'; ++ch)
		{
			if (!((*ch >= '0' && *ch <= '9') || *ch == '+' || *ch == '-' ||
				*ch == '.' || *ch == 'e' || *ch == 'E'))
				return false;
		}

		std::istringstream input(text);
		input.imbue(std::locale::classic());
		double parsed_db = 0.0;
		input >> std::noskipws >> parsed_db;
		if (!input || !input.eof() || !(parsed_db >= kMinDb && parsed_db <= kMaxDb))
			return false;

		gain_db = static_cast<float>(parsed_db);
		return true;
	}

	inline float startupGain(const char* saved_gain, const char* override_gain)
	{
		float gain_db = kDefaultDb;
		tryParse(saved_gain, gain_db);
		tryParse(override_gain, gain_db);
		return gain_db;
	}
}
