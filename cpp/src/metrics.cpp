#include "metrics.hpp"

#include <algorithm>
#include <numeric>

namespace antchain {

double gini(const std::vector<double>& values) {
    std::vector<double> x = values;
    double sum = std::accumulate(x.begin(), x.end(), 0.0);
    if (sum <= 0.0) return 0.0;
    std::sort(x.begin(), x.end());
    int n = static_cast<int>(x.size());
    double cum = 0.0, cum_sum_total = 0.0;
    std::vector<double> cum_vec(n);
    for (int i = 0; i < n; ++i) {
        cum += x[i];
        cum_vec[i] = cum;
    }
    cum_sum_total = std::accumulate(cum_vec.begin(), cum_vec.end(), 0.0);
    double last = cum_vec.back();
    return (n + 1 - 2.0 * (cum_sum_total / last)) / n;
}

} // namespace antchain
