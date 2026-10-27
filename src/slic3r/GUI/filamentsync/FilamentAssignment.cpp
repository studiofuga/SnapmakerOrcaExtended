#include "FilamentAssignment.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

#include "slic3r/Utils/ColorSpaceConvert.hpp"

namespace {

// Cost of a pair involving a column that does not exist: any real pair must win over it.
constexpr double k_dummy_cost = 1e6;
// Cost of a pair the options rule out: avoided unless there is nothing else at all.
constexpr double k_forbidden_cost = 1e9;
// Large enough to act as infinity in the solver, small enough that subtracting from it is safe.
constexpr double k_solver_inf = 1e18;
// A spool with no usable color must lose to every spool that has one (CIEDE2000 tops out near 100).
constexpr float k_unknown_color_cost = 50.f;

int hex_digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

// "#RRGGBB", "RRGGBB", "#RGB" and "#RRGGBBAA" (alpha ignored). false when unparsable.
bool parse_hex_color(const std::string& text, float& r, float& g, float& b)
{
    std::string hex;
    hex.reserve(text.size());
    for (char c : text) {
        if (c == '#' || std::isspace(static_cast<unsigned char>(c)))
            continue;
        if (hex_digit(c) < 0)
            return false;
        hex.push_back(c);
    }

    if (hex.size() == 3) {
        // #RGB is shorthand for #RRGGBB.
        std::string expanded;
        for (char c : hex) {
            expanded.push_back(c);
            expanded.push_back(c);
        }
        hex = expanded;
    }
    if (hex.size() != 6 && hex.size() != 8)
        return false;

    const int values[3] = {hex_digit(hex[0]) * 16 + hex_digit(hex[1]),
                           hex_digit(hex[2]) * 16 + hex_digit(hex[3]),
                           hex_digit(hex[4]) * 16 + hex_digit(hex[5])};
    // RGB2Lab expects the channels in [0, 1].
    r = float(values[0]) / 255.f;
    g = float(values[1]) / 255.f;
    b = float(values[2]) / 255.f;
    return true;
}

bool iequals_ascii(const std::string& lhs, const std::string& rhs)
{
    if (lhs.size() != rhs.size())
        return false;
    for (size_t i = 0; i < lhs.size(); ++i) {
        const unsigned char a = static_cast<unsigned char>(lhs[i]);
        const unsigned char b = static_cast<unsigned char>(rhs[i]);
        if (std::tolower(a) != std::tolower(b))
            return false;
    }
    return true;
}

} // namespace

namespace Slic3r {

float color_delta_e(const std::string& lhs, const std::string& rhs)
{
    float r1, g1, b1, r2, g2, b2;
    if (!parse_hex_color(lhs, r1, g1, b1) || !parse_hex_color(rhs, r2, g2, b2))
        return -1.f;

    float L1 = 0.f, a1 = 0.f, bb1 = 0.f, L2 = 0.f, a2 = 0.f, bb2 = 0.f;
    RGB2Lab(r1, g1, b1, &L1, &a1, &bb1);
    RGB2Lab(r2, g2, b2, &L2, &a2, &bb2);
    return DeltaE00(L1, a1, bb1, L2, a2, bb2);
}

double assignment_cost(const AssignmentSlot&      slot,
                       const AssignmentCandidate& candidate,
                       const AssignmentOptions&   options)
{
    const bool type_comparable = !slot.type.empty() && !candidate.type.empty();
    const bool type_differs    = type_comparable && !iequals_ascii(slot.type, candidate.type);
    if (type_differs && options.forbid_type_mismatch)
        return std::numeric_limits<double>::infinity();

    const float delta = color_delta_e(slot.design_color, candidate.color);
    double      cost  = delta < 0.f ? double(k_unknown_color_cost) : double(delta);

    if (type_differs)
        cost += double(options.type_mismatch_penalty);

    if (slot.required_weight > 0. && candidate.available_weight >= 0. &&
        candidate.available_weight < slot.required_weight)
        cost += double(options.short_spool_penalty);

    return cost;
}

std::vector<int> solve_min_cost_assignment(const std::vector<std::vector<double>>& cost)
{
    const size_t rows = cost.size();
    if (rows == 0)
        return {};
    const size_t cols = cost[0].size();
    for (const std::vector<double>& row : cost) {
        if (row.size() != cols)
            return std::vector<int>(rows, -1); // malformed input: assign nothing
    }
    if (cols == 0)
        return std::vector<int>(rows, -1);

    // Jonker-Volgenant needs rows <= cols, so the matrix is squared off with dummy columns.
    const size_t n = std::max(rows, cols);

    // 1-based, as the algorithm indexes from 1 and uses index 0 as a sentinel.
    std::vector<std::vector<double>> a(n + 1, std::vector<double>(n + 1, k_dummy_cost));
    for (size_t i = 1; i <= rows; ++i) {
        for (size_t j = 1; j <= cols; ++j) {
            const double c = cost[i - 1][j - 1];
            a[i][j] = (std::isfinite(c) && c < k_forbidden_cost) ? c : k_forbidden_cost;
        }
    }

    std::vector<double> u(n + 1, 0.), v(n + 1, 0.), minv(n + 1, 0.);
    std::vector<size_t> p(n + 1, 0), way(n + 1, 0);
    std::vector<char>   used(n + 1, 0);

    for (size_t i = 1; i <= n; ++i) {
        p[0] = i;
        size_t j0 = 0;
        std::fill(minv.begin(), minv.end(), k_solver_inf);
        std::fill(used.begin(), used.end(), char(0));
        do {
            used[j0]       = 1;
            const size_t i0 = p[j0];
            double       delta = k_solver_inf;
            size_t       j1    = 0;
            for (size_t j = 1; j <= n; ++j) {
                if (used[j])
                    continue;
                const double cur = a[i0][j] - u[i0] - v[j];
                if (cur < minv[j]) {
                    minv[j] = cur;
                    way[j]  = j0;
                }
                if (minv[j] < delta) {
                    delta = minv[j];
                    j1    = j;
                }
            }
            for (size_t j = 0; j <= n; ++j) {
                if (used[j]) {
                    u[p[j]] += delta;
                    v[j] -= delta;
                } else {
                    minv[j] -= delta;
                }
            }
            j0 = j1;
        } while (p[j0] != 0);
        // Walk the augmenting path back, rotating the matches along it.
        do {
            const size_t j1 = way[j0];
            p[j0] = p[j1];
            j0    = j1;
        } while (j0 != 0);
    }

    std::vector<int> result(rows, -1);
    for (size_t j = 1; j <= cols; ++j) {
        const size_t i = p[j];
        if (i < 1 || i > rows)
            continue; // a dummy row: this column stays free
        const double c = cost[i - 1][j - 1];
        if (std::isfinite(c) && c < k_forbidden_cost)
            result[i - 1] = int(j - 1);
    }
    return result;
}

std::vector<int> compute_optimal_assignment(const std::vector<AssignmentSlot>&      slots,
                                            const std::vector<AssignmentCandidate>& candidates,
                                            const AssignmentOptions&                options)
{
    std::vector<int> result(slots.size(), -1);
    if (slots.empty() || candidates.empty())
        return result;

    // The pins are what the user asked for: they are honored first, and when the candidates are
    // exclusive they leave the pool before anyone else chooses.
    std::vector<char>   taken(candidates.size(), 0);
    std::vector<size_t> free_slots;
    for (size_t i = 0; i < slots.size(); ++i) {
        if (slots[i].skipped)
            continue;
        const int pin = slots[i].pinned;
        if (pin >= 0 && size_t(pin) < candidates.size()) {
            result[i] = pin;
            taken[size_t(pin)] = 1;
        } else {
            free_slots.push_back(i);
        }
    }
    if (free_slots.empty())
        return result;

    if (!options.exclusive) {
        for (size_t i : free_slots) {
            double best      = std::numeric_limits<double>::infinity();
            int    best_cand = -1;
            for (size_t j = 0; j < candidates.size(); ++j) {
                const double c = assignment_cost(slots[i], candidates[j], options);
                if (std::isfinite(c) && c < best) {
                    best      = c;
                    best_cand = int(j);
                }
            }
            result[i] = best_cand;
        }
        return result;
    }

    std::vector<size_t> pool;
    pool.reserve(candidates.size());
    for (size_t j = 0; j < candidates.size(); ++j) {
        if (!taken[j])
            pool.push_back(j);
    }
    if (pool.empty())
        return result;

    std::vector<std::vector<double>> cost(free_slots.size(), std::vector<double>(pool.size(), 0.));
    for (size_t r = 0; r < free_slots.size(); ++r) {
        for (size_t c = 0; c < pool.size(); ++c)
            cost[r][c] = assignment_cost(slots[free_slots[r]], candidates[pool[c]], options);
    }

    const std::vector<int> solved = solve_min_cost_assignment(cost);
    for (size_t r = 0; r < solved.size() && r < free_slots.size(); ++r) {
        if (solved[r] >= 0 && size_t(solved[r]) < pool.size())
            result[free_slots[r]] = int(pool[size_t(solved[r])]);
    }
    return result;
}

} // namespace Slic3r
