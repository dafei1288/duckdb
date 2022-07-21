//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/common/bitpacking.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "bitpackinghelpers.h"
#include "duckdb/common/assert.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/helper.hpp"
#include "duckdb/common/limits.hpp"

namespace duckdb {

using bitpacking_width_t = uint8_t;

class BitpackingPrimitives {

public:
	static constexpr const idx_t BITPACKING_ALGORITHM_GROUP_SIZE = 32;
	static constexpr const idx_t BITPACKING_HEADER_SIZE = sizeof(uint64_t);
	static constexpr const bool BYTE_ALIGNED = false;

	// To ensure enough data is available, use GetRequiredSize() to determine the correct size for dst buffer
	// Note: input should be aligned to BITPACKING_ALGORITHM_GROUP_SIZE for good performance.
	template <class T, bool ASSUME_INPUT_ALIGNED = false>
	inline static void PackBuffer(data_ptr_t dst, T *src, idx_t count, bitpacking_width_t width) {
		if (ASSUME_INPUT_ALIGNED) {
			for (idx_t i = 0; i < count; i += BITPACKING_ALGORITHM_GROUP_SIZE) {
				PackGroup<T>(dst + (i * width) / 8, src + i, width);
			}
		} else {
			idx_t misaligned_count = count % BITPACKING_ALGORITHM_GROUP_SIZE;
			T tmp_buffer[BITPACKING_ALGORITHM_GROUP_SIZE]; // TODO maybe faster on the heap?

			if (misaligned_count) {
				count -= misaligned_count;
			}

			for (idx_t i = 0; i < count; i += BITPACKING_ALGORITHM_GROUP_SIZE) {
				PackGroup<T>(dst + (i * width) / 8, src + i, width);
			}

			// Input was not aligned to BITPACKING_ALGORITHM_GROUP_SIZE, we need a copy
			if (misaligned_count) {
				memcpy(tmp_buffer, src + count, misaligned_count * sizeof(T));
				PackGroup<T>(dst + (count * width) / 8, tmp_buffer, width);
			}
		}
	}

	// Unpacks a block of BITPACKING_ALGORITHM_GROUP_SIZE values
	// Assumes both src and dst to be of the correct size
	template <class T>
	inline static void UnPackBuffer(data_ptr_t dst, data_ptr_t src, idx_t count, bitpacking_width_t width,
	                                T frame_of_reference, bool skip_sign_extension = false) {

		for (idx_t i = 0; i < count; i += BITPACKING_ALGORITHM_GROUP_SIZE) {
			UnPackGroup<T>(dst + i * sizeof(T), src + (i * width) / 8, width, frame_of_reference, skip_sign_extension);
		}
	}

	// Packs a block of BITPACKING_ALGORITHM_GROUP_SIZE values
	template <class T>
	inline static void PackBlock(data_ptr_t dst, T *src, bitpacking_width_t width) {
		return PackGroup<T>(dst, src, width);
	}

	// Unpacks a block of BITPACKING_ALGORITHM_GROUP_SIZE values
	template <class T>
	inline static void UnPackBlock(data_ptr_t dst, data_ptr_t src, bitpacking_width_t width, T frame_of_reference,
	                               bool skip_sign_extension = false) {
		return UnPackGroup<T>(dst, src, width, frame_of_reference, skip_sign_extension);
	}

	// Calculates the minimum required number of bits per value that can store all values
	template <class T>
	inline static bitpacking_width_t MinimumBitWidth(T value) {
		return FindMinimumBitWidth<T, BYTE_ALIGNED>(value, value);
	}

	// Calculates the minimum required number of bits per value that can store all values
	template <class T>
	inline static bitpacking_width_t MinimumBitWidth(T *values, idx_t count) {
		return FindMinimumBitWidth<T, BYTE_ALIGNED>(values, count);
	}

	template <class T>
	inline static idx_t GetRequiredSize(idx_t count, bitpacking_width_t width) {
		count = RoundUpToAlgorithmGroupSize(count);
		return ((count * width) / 8);
	}

	template <class T>
	inline static T RoundUpToAlgorithmGroupSize(T num_to_round) {
		int remainder = num_to_round % BITPACKING_ALGORITHM_GROUP_SIZE;
		if (remainder == 0) {
			return num_to_round;
		}

		return num_to_round + BITPACKING_ALGORITHM_GROUP_SIZE - remainder;
	}

private:
	template <class T, bool round_to_next_byte = false>
	static bitpacking_width_t FindMinimumBitWidth(T *values, idx_t count) {
		T min_value = values[0];
		T max_value = values[0];

		for (idx_t i = 1; i < count; i++) {
			if (values[i] > max_value) {
				max_value = values[i];
			}

			if (std::is_signed<T>::value) {
				if (values[i] < min_value) {
					min_value = values[i];
				}
			}
		}

		return FindMinimumBitWidth<T, round_to_next_byte>(min_value, max_value);
	}

	template <class T, bool round_to_next_byte = false>
	static bitpacking_width_t FindMinimumBitWidth(T min_value, T max_value) {
		bitpacking_width_t bitwidth;
		T value;

		if (std::is_signed<T>::value) {
			if (min_value == NumericLimits<T>::Minimum()) {
				// handle special case of the minimal value, as it cannot be negated like all other values.
				return sizeof(T) * 8;
			} else {
				value = MaxValue((T)-min_value, max_value);
			}
		} else {
			value = max_value;
		}

		if (value == 0) {
			return 0;
		}

		if (std::is_signed<T>::value) {
			bitwidth = 1;
		} else {
			bitwidth = 0;
		}

		while (value) {
			bitwidth++;
			value >>= 1;
		}

		bitwidth = GetEffectiveWidth<T>(bitwidth);

		// Assert results are correct
#ifdef DEBUG
		if (bitwidth < sizeof(T) * 8 && bitwidth != 0) {
			if (std::is_signed<T>::value) {
				D_ASSERT((int64_t)max_value <= (int64_t)(1L << (bitwidth - 1)) - 1);
				D_ASSERT((int64_t)min_value >= (int64_t)(-1 * ((1L << (bitwidth - 1)) - 1) - 1));
			} else {
				D_ASSERT((uint64_t)max_value <= (uint64_t)(1L << (bitwidth)) - 1);
			}
		}
#endif
		if (round_to_next_byte) {
			return (bitwidth / 8 + (bitwidth % 8 != 0)) * 8;
		} else {
			return bitwidth;
		}
	}

	template <class T, class T_U = typename std::make_unsigned<T>::type>
	static void ApplyFrameOfReference(data_ptr_t dst, T frame_of_reference) {
		if (!frame_of_reference) {
			return;
		}
		for (idx_t i = 0; i < BITPACKING_ALGORITHM_GROUP_SIZE; i++) {
			T_U value = Load<T_U>(dst + i * sizeof(T));
			T adjusted_value = (T)(value + (T_U)frame_of_reference);
			Store<T>(adjusted_value, dst + i * sizeof(T));
		}
	}

	template <class T>
	static void UnPackGroup(data_ptr_t dst, data_ptr_t src, bitpacking_width_t width, T frame_of_reference,
	                        bool skip_sign_extension = false) {
		if (std::is_same<T, uint8_t>::value || std::is_same<T, int8_t>::value) {
			duckdb_fastpforlib::fastunpack((const uint8_t *)src, (uint8_t *)dst, (uint32_t)width);
		} else if (std::is_same<T, uint16_t>::value || std::is_same<T, int16_t>::value) {
			duckdb_fastpforlib::fastunpack((const uint16_t *)src, (uint16_t *)dst, (uint32_t)width);
		} else if (std::is_same<T, uint32_t>::value || std::is_same<T, int32_t>::value) {
			duckdb_fastpforlib::fastunpack((const uint32_t *)src, (uint32_t *)dst, (uint32_t)width);
		} else if (std::is_same<T, uint64_t>::value || std::is_same<T, int64_t>::value) {
			duckdb_fastpforlib::fastunpack((const uint32_t *)src, (uint64_t *)dst, (uint32_t)width);
		} else {
			throw InternalException("Unsupported type found in bitpacking.");
		}

		ApplyFrameOfReference(dst, frame_of_reference);
	}

	// Prevent compression at widths that are ineffective
	template <class T>
	static bitpacking_width_t GetEffectiveWidth(bitpacking_width_t width) {
		auto bits_of_type = sizeof(T) * 8;
		auto type_size = sizeof(T);
		if (width + type_size > bits_of_type) {
			return bits_of_type;
		}
		return width;
	}

	template <class T>
	static void PackGroup(data_ptr_t dst, T *values, bitpacking_width_t width) {
		if (std::is_same<T, uint8_t>::value || std::is_same<T, int8_t>::value) {
			duckdb_fastpforlib::fastpack((const uint8_t *)values, (uint8_t *)dst, (uint32_t)width);
		} else if (std::is_same<T, uint16_t>::value || std::is_same<T, int16_t>::value) {
			duckdb_fastpforlib::fastpack((const uint16_t *)values, (uint16_t *)dst, (uint32_t)width);
		} else if (std::is_same<T, uint32_t>::value || std::is_same<T, int32_t>::value) {
			duckdb_fastpforlib::fastpack((const uint32_t *)values, (uint32_t *)dst, (uint32_t)width);
		} else if (std::is_same<T, uint64_t>::value || std::is_same<T, int64_t>::value) {
			duckdb_fastpforlib::fastpack((const uint64_t *)values, (uint32_t *)dst, (uint32_t)width);
		} else {
			throw InternalException("Unsupported type found in bitpacking.");
		}
	}
};

} // namespace duckdb
