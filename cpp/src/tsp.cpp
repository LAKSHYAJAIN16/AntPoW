#include "tsp.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>

namespace antchain {

double TSPInstance::tour_length(const std::vector<int>& tour) const {
    double total = 0.0;
    const int n = static_cast<int>(tour.size());
    for (int i = 0; i < n; ++i) {
        total += d(tour[i], tour[(i + 1) % n]);
    }
    return total;
}

bool TSPInstance::is_valid_tour(const std::vector<int>& tour) const {
    if (static_cast<int>(tour.size()) != n_cities) return false;
    std::vector<bool> seen(n_cities, false);
    for (int c : tour) {
        if (c < 0 || c >= n_cities || seen[c]) return false;
        seen[c] = true;
    }
    return true;
}

TSPInstance generate_instance(uint64_t seed, int n_cities) {
    SplitMix64 rng(seed);
    TSPInstance inst;
    inst.n_cities = n_cities;
    inst.xs.resize(n_cities);
    inst.ys.resize(n_cities);
    for (int i = 0; i < n_cities; ++i) {
        inst.xs[i] = rng.next_double();
        inst.ys[i] = rng.next_double();
    }
    inst.dist.assign(static_cast<size_t>(n_cities) * n_cities, 0.0);
    for (int i = 0; i < n_cities; ++i) {
        for (int j = 0; j < n_cities; ++j) {
            double dx = inst.xs[i] - inst.xs[j];
            double dy = inst.ys[i] - inst.ys[j];
            inst.dist[static_cast<size_t>(i) * n_cities + j] = std::sqrt(dx * dx + dy * dy);
        }
    }
    return inst;
}

std::vector<int> nearest_neighbor_tour(const TSPInstance& inst, int start) {
    const int n = inst.n_cities;
    std::vector<bool> visited(n, false);
    std::vector<int> tour;
    tour.reserve(n);
    tour.push_back(start);
    visited[start] = true;
    int current = start;
    for (int step = 1; step < n; ++step) {
        int best = -1;
        double best_d = 1e18;
        for (int c = 0; c < n; ++c) {
            if (!visited[c] && inst.d(current, c) < best_d) {
                best_d = inst.d(current, c);
                best = c;
            }
        }
        tour.push_back(best);
        visited[best] = true;
        current = best;
    }
    return tour;
}

AntColonyOptimizer::AntColonyOptimizer(const TSPInstance& inst, uint64_t seed, int n_ants,
                                        double alpha, double beta, double rho, double q)
    : inst_(&inst), n_ants_(n_ants), alpha_(alpha), beta_(beta), rho_(rho), q_(q), rng_(seed) {
    reset(inst);
}

void AntColonyOptimizer::reset(const TSPInstance& inst) {
    inst_ = &inst;
    const int n = inst.n_cities;
    tau_.assign(static_cast<size_t>(n) * n, 1.0);
    eta_.assign(static_cast<size_t>(n) * n, 0.0);
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            double dij = inst.d(i, j);
            eta_[static_cast<size_t>(i) * n + j] = dij > 0.0 ? 1.0 / dij : 0.0;
        }
    }
    best_tour.clear();
    best_length = 1e18;
}

void AntColonyOptimizer::rebind_instance_keep_pheromones(const TSPInstance& inst) {
    inst_ = &inst;
    const int n = inst.n_cities;
    eta_.assign(static_cast<size_t>(n) * n, 0.0);
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            double dij = inst.d(i, j);
            eta_[static_cast<size_t>(i) * n + j] = dij > 0.0 ? 1.0 / dij : 0.0;
        }
    }
    best_tour.clear();
    best_length = 1e18;
}

std::vector<int> AntColonyOptimizer::construct_tour() {
    const int n = inst_->n_cities;
    std::vector<bool> visited(n, false);
    std::vector<int> tour;
    tour.reserve(n);
    int start = rng_.next_int(n);
    tour.push_back(start);
    visited[start] = true;
    int current = start;

    std::vector<double> weights(n);
    for (int step = 1; step < n; ++step) {
        double total = 0.0;
        for (int c = 0; c < n; ++c) {
            if (visited[c]) {
                weights[c] = 0.0;
                continue;
            }
            double tau = tau_[static_cast<size_t>(current) * n + c];
            double eta = eta_[static_cast<size_t>(current) * n + c];
            weights[c] = std::pow(tau, alpha_) * std::pow(eta, beta_);
            total += weights[c];
        }
        int next = -1;
        if (total <= 0.0) {
            // no distinguishing weight (degenerate case) -- pick uniformly
            // among unvisited cities
            int remaining = n - step;
            int pick = rng_.next_int(remaining);
            for (int c = 0; c < n; ++c) {
                if (!visited[c]) {
                    if (pick == 0) { next = c; break; }
                    --pick;
                }
            }
        } else {
            double r = rng_.next_double() * total;
            double cum = 0.0;
            for (int c = 0; c < n; ++c) {
                if (visited[c]) continue;
                cum += weights[c];
                if (r <= cum) { next = c; break; }
            }
            if (next == -1) {
                for (int c = n - 1; c >= 0; --c) if (!visited[c]) { next = c; break; }
            }
        }
        tour.push_back(next);
        visited[next] = true;
        current = next;
    }
    return tour;
}

std::pair<std::vector<int>, double> AntColonyOptimizer::step(int n_iterations) {
    const int n = inst_->n_cities;
    for (int it = 0; it < n_iterations; ++it) {
        std::vector<std::vector<int>> tours(n_ants_);
        std::vector<double> lengths(n_ants_);
        for (int k = 0; k < n_ants_; ++k) {
            tours[k] = construct_tour();
            lengths[k] = inst_->tour_length(tours[k]);
        }
        for (auto& v : tau_) v *= (1.0 - rho_);
        for (int k = 0; k < n_ants_; ++k) {
            if (lengths[k] <= 0.0) continue;
            double deposit = q_ / lengths[k];
            const auto& t = tours[k];
            for (int i = 0; i < n; ++i) {
                int a = t[i];
                int b = t[(i + 1) % n];
                tau_[static_cast<size_t>(a) * n + b] += deposit;
                tau_[static_cast<size_t>(b) * n + a] += deposit;
            }
        }
        int best_idx = static_cast<int>(std::min_element(lengths.begin(), lengths.end()) - lengths.begin());
        if (lengths[best_idx] < best_length) {
            best_length = lengths[best_idx];
            best_tour = tours[best_idx];
        }
    }
    return {best_tour, best_length};
}

} // namespace antchain
