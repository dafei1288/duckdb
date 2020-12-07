//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/execution/reservoir_sample.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/common.hpp"
#include "duckdb/common/types/chunk_collection.hpp"
#include "duckdb/common/limits.hpp"
#include <random>
#include <queue>

namespace duckdb {

struct RandomEngine {
	std::mt19937 random_engine;
	RandomEngine(int64_t seed) {
		if (seed < 0) {
			std::random_device rd;
			random_engine.seed(rd());
		} else {
			random_engine.seed(seed);
		}
	}

	//! Generate a random number between min and max
	double NextRandom(double min, double max) {
		std::uniform_real_distribution<double> dist(min, max);
		return dist(random_engine);
	}
	//! Generate a random number between 0 and 1
	double NextRandom() {
		return NextRandom(0, 1);
	}
	uint32_t NextRandomInteger() {
		std::uniform_int_distribution<uint32_t> dist(0, NumericLimits<uint32_t>::Maximum());
		return dist(random_engine);
	}
};

class BlockingSample {
public:
	BlockingSample(int64_t seed) :
		random(seed) {}
	virtual ~BlockingSample(){}

	//! Add a chunk of data to the sample
	virtual void AddToReservoir(DataChunk &input) = 0;

	//! Fetches a chunk from the sample. Note that this method is destructive and should only be used after the
	// sample is completely built.
	virtual unique_ptr<DataChunk> GetChunk() = 0;

protected:
	//! The random generator
	RandomEngine random;
};

//! The reservoir sample class maintains a streaming sample of fixed size "sample_count"
class ReservoirSample : public BlockingSample {
public:
	ReservoirSample(idx_t sample_count, int64_t seed) :
		BlockingSample(seed), sample_count(sample_count) {}


	//! Add a chunk of data to the sample
	void AddToReservoir(DataChunk &input) override;

	//! Fetches a chunk from the sample. Note that this method is destructive and should only be used after the
	// sample is completely built.
	unique_ptr<DataChunk> GetChunk() override;
private:
	//! Sets the next index to insert into the reservoir based on the reservoir weights
	void SetNextEntry();

	//! Replace a single element of the input
	void ReplaceElement(DataChunk &input, idx_t index_in_chunk);

	//! Fills the reservoir up until sample_count entries, returns how many entries are still required
	idx_t FillReservoir(DataChunk &input);

private:
	//! The size of the reservoir sample
	idx_t sample_count;
	//! The current reservoir
	ChunkCollection reservoir;
	//! Priority queue of [random element, index] for each of the elements in the sample
	std::priority_queue<std::pair<double, idx_t>> reservoir_weights;
	//! The next element to sample
	idx_t next_index;
	//! The reservoir threshold of the current min entry
	double min_threshold;
	//! The reservoir index of the current min entry
	idx_t min_entry;
	//! The current count towards next index (i.e. we will replace an entry in next_index - current_count tuples)
	idx_t current_count;
};

//! The reservoir sample percentage class maintains a streaming sample of variable size
class ReservoirSamplePercentage : public BlockingSample {
	constexpr static idx_t RESERVOIR_THRESHOLD = STANDARD_VECTOR_SIZE * 100;
public:
	ReservoirSamplePercentage(double percentage, int64_t seed);

	//! Add a chunk of data to the sample
	void AddToReservoir(DataChunk &input) override;

	//! Fetches a chunk from the sample. Note that this method is destructive and should only be used after the
	// sample is completely built.
	unique_ptr<DataChunk> GetChunk() override;

private:
	void Finalize();

private:
	//! The percentage to sample
	double sample_percentage;
	//! The fixed sample size of the sub-reservoirs
	idx_t reservoir_sample_size;
	//! The current sample
	unique_ptr<ReservoirSample> current_sample;
	//! The set of finished samples of the reservoir sample
	vector<unique_ptr<ReservoirSample>> finished_samples;
	//! The amount of tuples that have been processed so far
	idx_t current_count = 0;
	//! Whether or not the stream is finalized. The stream is automatically finalized on the first call to GetChunk();
	bool is_finalized;
};

} // namespace duckdb
