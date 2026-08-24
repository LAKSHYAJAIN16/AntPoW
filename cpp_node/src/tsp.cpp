#include "tsp.hpp"

#include <algorithm>
#include <cmath>

#include "hex.hpp"

namespace antchain_node {

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
    std::vector<bool> seen(static_cast<size_t>(n_cities), false);
    for (int c : tour) {
        if (c < 0 || c >= n_cities || seen[static_cast<size_t>(c)]) return false;
        seen[static_cast<size_t>(c)] = true;
    }
    return true;
}

uint64_t seed_from_hash(const Hash32& prev_hash) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | prev_hash[static_cast<size_t>(i)];
    return v & 0x7FFFFFFFFFFFFFFFULL;
}

uint64_t seed_from_hash_hex(const std::string& prev_hash_hex) {
    auto bytes = from_hex(prev_hash_hex);
    uint64_t v = 0;
    for (int i = 0; i < 8 && static_cast<size_t>(i) < bytes.size(); ++i) v = (v << 8) | bytes[static_cast<size_t>(i)];
    return v & 0x7FFFFFFFFFFFFFFFULL;
}

TSPInstance generate_instance(uint64_t seed, int n_cities) {
    SplitMix64 rng(seed);
    TSPInstance inst;
    inst.n_cities = n_cities;
    inst.xs.resize(static_cast<size_t>(n_cities));
    inst.ys.resize(static_cast<size_t>(n_cities));
    for (int i = 0; i < n_cities; ++i) {
        inst.xs[static_cast<size_t>(i)] = rng.next_double();
        inst.ys[static_cast<size_t>(i)] = rng.next_double();
    }
    inst.dist.assign(static_cast<size_t>(n_cities) * static_cast<size_t>(n_cities), 0.0);
    for (int i = 0; i < n_cities; ++i) {
        for (int j = 0; j < n_cities; ++j) {
            double dx = inst.xs[static_cast<size_t>(i)] - inst.xs[static_cast<size_t>(j)];
            double dy = inst.ys[static_cast<size_t>(i)] - inst.ys[static_cast<size_t>(j)];
            inst.dist[static_cast<size_t>(i) * static_cast<size_t>(n_cities) + static_cast<size_t>(j)] =
                std::sqrt(dx * dx + dy * dy);
        }
    }
    return inst;
}

std::vector<int> nearest_neighbor_tour(const TSPInstance& inst, int start) {
    const int n = inst.n_cities;
    std::vector<bool> visited(static_cast<size_t>(n), false);
    std::vector<int> tour;
    tour.reserve(static_cast<size_t>(n));
    tour.push_back(start);
    visited[static_cast<size_t>(start)] = true;
    int current = start;
    for (int step = 1; step < n; ++step) {
        int best = -1;
        double best_d = 1e18;
        for (int c = 0; c < n; ++c) {
            if (!visited[static_cast<size_t>(c)] && inst.d(current, c) < best_d) {
                best_d = inst.d(current, c);
                best = c;
            }
        }
        tour.push_back(best);
        visited[static_cast<size_t>(best)] = true;
        current = best;
    }
    return tour;
}

double reference_length(const TSPInstance& inst) {
    return inst.tour_length(nearest_neighbor_tour(inst, 0));
}

AntColonyOptimizer::AntColonyOptimizer(const TSPInstance& inst, uint64_t seed, int n_ants,
                                        double alpha, double beta, double rho, double q)
    : inst_(&inst), n_ants_(n_ants), alpha_(alpha), beta_(beta), rho_(rho), q_(q), rng_(seed) {
    reset(inst);
}

void AntColonyOptimizer::reset(const TSPInstance& inst) {
    inst_ = &inst;
    const int n = inst.n_cities;
    tau_.assign(static_cast<size_t>(n) * static_cast<size_t>(n), 1.0);
    eta_.assign(static_cast<size_t>(n) * static_cast<size_t>(n), 0.0);
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            double dij = inst.d(i, j);
            eta_[static_cast<size_t>(i) * static_cast<size_t>(n) + static_cast<size_t>(j)] = dij > 0.0 ? 1.0 / dij : 0.0;
        }
    }
    best_tour.clear();
    best_length = 1e18;
}

std::vector<int> AntColonyOptimizer::construct_tour() {
    const int n = inst_->n_cities;
    std::vector<bool> visited(static_cast<size_t>(n), false);
    std::vector<int> tour;
    tour.reserve(static_cast<size_t>(n));
    int start = rng_.next_int(n);
    tour.push_back(start);
    visited[static_cast<size_t>(start)] = true;
    int current = start;

    std::vector<double> weights(static_cast<size_t>(n));
    for (int step = 1; step < n; ++step) {
        double total = 0.0;
        for (int c = 0; c < n; ++c) {
            if (visited[static_cast<size_t>(c)]) {
                weights[static_cast<size_t>(c)] = 0.0;
                continue;
            }
            double tau = tau_[static_cast<size_t>(current) * static_cast<size_t>(n) + static_cast<size_t>(c)];
            double eta = eta_[static_cast<size_t>(current) * static_cast<size_t>(n) + static_cast<size_t>(c)];
            weights[static_cast<size_t>(c)] = std::pow(tau, alpha_) * std::pow(eta, beta_);
            total += weights[static_cast<size_t>(c)];
        }
        int next = -1;
        if (total <= 0.0) {
            int remaining = n - step;
            int pick = rng_.next_int(remaining);
            for (int c = 0; c < n; ++c) {
                if (!visited[static_cast<size_t>(c)]) {
                    if (pick == 0) { next = c; break; }
                    --pick;
                }
            }
        } else {
            double r = rng_.next_double() * total;
            double cum = 0.0;
            for (int c = 0; c < n; ++c) {
                if (visited[static_cast<size_t>(c)]) continue;
                cum += weights[static_cast<size_t>(c)];
                if (r <= cum) { next = c; break; }
            }
            if (next == -1) {
                for (int c = n - 1; c >= 0; --c) if (!visited[static_cast<size_t>(c)]) { next = c; break; }
            }
        }
        tour.push_back(next);
        visited[static_cast<size_t>(next)] = true;
        current = next;
    }
    return tour;
}

std::pair<std::vector<int>, double> AntColonyOptimizer::step(int n_iterations) {
    const int n = inst_->n_cities;
    for (int it = 0; it < n_iterations; ++it) {
        std::vector<std::vector<int>> tours(static_cast<size_t>(n_ants_));
        std::vector<double> lengths(static_cast<size_t>(n_ants_));
        for (int k = 0; k < n_ants_; ++k) {
            tours[static_cast<size_t>(k)] = construct_tour();
            lengths[static_cast<size_t>(k)] = inst_->tour_length(tours[static_cast<size_t>(k)]);
        }
        for (auto& v : tau_) v *= (1.0 - rho_);
        for (int k = 0; k < n_ants_; ++k) {
            if (lengths[static_cast<size_t>(k)] <= 0.0) continue;
            double deposit = q_ / lengths[static_cast<size_t>(k)];
            const auto& t = tours[static_cast<size_t>(k)];
            for (int i = 0; i < n; ++i) {
                int a = t[static_cast<size_t>(i)];
                int b = t[static_cast<size_t>((i + 1) % n)];
                tau_[static_cast<size_t>(a) * static_cast<size_t>(n) + static_cast<size_t>(b)] += deposit;
                tau_[static_cast<size_t>(b) * static_cast<size_t>(n) + static_cast<size_t>(a)] += deposit;
            }
        }
        int best_idx = static_cast<int>(std::min_element(lengths.begin(), lengths.end()) - lengths.begin());
        if (lengths[static_cast<size_t>(best_idx)] < best_length) {
            best_length = lengths[static_cast<size_t>(best_idx)];
            best_tour = tours[static_cast<size_t>(best_idx)];
        }
    }
    return {best_tour, best_length};
}

} // namespace antchain_node
