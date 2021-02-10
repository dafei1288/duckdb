#include "duckdb/common/exception.hpp"
#include "duckdb/common/vector_operations/vector_operations.hpp"
#include "duckdb/function/aggregate/distributive_functions.hpp"
#include "duckdb/planner/expression/bound_aggregate_expression.hpp"

namespace duckdb {

struct skew_state_t {
	size_t n;
	double sum;
	double sum_sqr;
	double sum_cub;
};

struct SkewnessOperation {
	template <class STATE>
	static void Initialize(STATE *state) {
		state->n = 0;
		state->sum = state->sum_sqr = state->sum_cub = 0;
	}

	template <class INPUT_TYPE, class STATE, class OP>
	static void ConstantOperation(STATE *state, FunctionData *bind_data, INPUT_TYPE *input, nullmask_t &nullmask,
	                              idx_t count) {
		for (idx_t i = 0; i < count; i++) {
			Operation<INPUT_TYPE, STATE, OP>(state, bind_data, input, nullmask, 0);
		}
	}

	template <class INPUT_TYPE, class STATE, class OP>
	static void Operation(STATE *state, FunctionData *bind_data_, INPUT_TYPE *data, nullmask_t &nullmask, idx_t idx) {
		if (nullmask[idx]) {
			return;
		}

		state->n++;
		state->sum += data[idx];
		state->sum_sqr += pow(data[idx], 2);
		state->sum_cub += pow(data[idx], 3);
	}

	template <class STATE, class OP>
	static void Combine(STATE source, STATE *target) {
		if (source.n == 0) {
			return;
		}

		target->n += source.n;
		target->sum += source.sum;
		target->sum_sqr += source.sum_sqr;
		target->sum_cub += source.sum_cub;
	}

	template <class TARGET_TYPE, class STATE>
	static void Finalize(Vector &result, FunctionData *bind_data_, STATE *state, TARGET_TYPE *target,
	                     nullmask_t &nullmask, idx_t idx) {
		if (state->n == 0) {
			nullmask[idx] = true;
			return;
		}
		double n = state->n;
		double temp = 1 / n;
		double temp1 = std::sqrt(n * (n - 1)) / (n - 2);
		target[idx] = temp1 * temp *
		              (state->sum_cub - 3 * state->sum_sqr * state->sum * temp + 2 * pow(state->sum, 3) * temp * temp) /
		              (std::sqrt(std::pow(temp * (state->sum_sqr - state->sum * state->sum * temp), 3)));
		if (!Value::DoubleIsValid(target[idx])) {
			nullmask[idx] = true;
		}
	}

	static bool IgnoreNull() {
		return true;
	}
};

void SkewFun::RegisterFunction(BuiltinFunctions &set) {
	AggregateFunctionSet functionSet("skewness");
	functionSet.AddFunction(AggregateFunction::UnaryAggregate<skew_state_t, double, double, SkewnessOperation>(
	    LogicalType::DOUBLE, LogicalType::DOUBLE));
	set.AddFunction(functionSet);
}

} // namespace duckdb