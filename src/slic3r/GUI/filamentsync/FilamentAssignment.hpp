#pragma once

#include <string>
#include <vector>

namespace Slic3r {

// Assignment of the filament slots of a project to the physical filaments of an inventory
// (the Spoolman spools, or the trays of a printer).
//
// The problem is NOT "for each slot, pick the closest color": the physical filaments are
// exclusive, so choosing greedily lets an early slot take the spool a later one needed much
// more.  What is computed here is the assignment with the lowest total cost over all slots at
// once (Hungarian / Jonker-Volgenant).
//
// Every cost is expressed in CIEDE2000 units so that the penalties stay comparable with the
// color distance itself: ~1 is invisible, ~2 just noticeable, ~5 clearly another shade, >10
// another color.  Keep in mind that the colors on both sides are nominal values typed into a
// profile or an inventory, not measurements: the ranking they produce is useful, the absolute
// figure is indicative.

// A filament slot of the project waiting for a physical filament.
struct AssignmentSlot
{
    std::string design_color;         // "#RRGGBB": the color the project was designed with
    std::string type;                 // "PLA", "PETG", ...; empty means no preference
    double      required_weight{0.};  // grams this slot needs; 0 when unknown (not sliced yet)
    int         pinned{-1};           // candidate the user locked on this slot, -1 when free
    bool        skipped{false};       // the user left this slot alone: never assigned
};

// A physical filament a slot can be assigned to.
struct AssignmentCandidate
{
    std::string color;                   // "#RRGGBB": the color actually loaded
    std::string type;                    // material as the inventory reports it
    double      available_weight{-1.};   // grams left; negative when unknown
};

struct AssignmentOptions
{
    // A different material is worse than any color miss ...
    float type_mismatch_penalty{30.f};
    // ... and a spool that cannot finish the slot worse still, yet neither is forbidden: the
    // user may want PETG on one specific extruder, or may be about to refill a spool.
    float short_spool_penalty{45.f};
    // true rules a different material out entirely instead of penalizing it.
    bool  forbid_type_mismatch{false};
    // false lets the same physical filament serve several slots.
    bool  exclusive{true};
};

// Perceptual distance (CIEDE2000) between two "#RRGGBB" colors.
// Returns a negative value when either color cannot be parsed.
float color_delta_e(const std::string& lhs, const std::string& rhs);

// Cost of putting "candidate" on "slot": the color distance plus the penalties, or infinity when
// the options forbid the pair.  Exposed so the UI can show the very number the solver minimizes.
double assignment_cost(const AssignmentSlot&     slot,
                       const AssignmentCandidate& candidate,
                       const AssignmentOptions&   options);

// The assignment with the lowest total cost: one entry per slot, holding the index of the
// candidate assigned to it, or -1 when the slot is left unassigned (skipped, no candidate left,
// or every pair forbidden).  Pinned slots keep their candidate and, when exclusive, take it out
// of the pool for everyone else.
std::vector<int> compute_optimal_assignment(const std::vector<AssignmentSlot>&      slots,
                                            const std::vector<AssignmentCandidate>& candidates,
                                            const AssignmentOptions&                options = AssignmentOptions());

// Minimum-cost assignment of rows to distinct columns, O(n^3) in max(rows, cols).
// "cost" must be rectangular; a non-finite entry, or one above 1e9, forbids that pair.
// Returns, for each row, the column assigned to it, or -1 when it got none.
std::vector<int> solve_min_cost_assignment(const std::vector<std::vector<double>>& cost);

} // namespace Slic3r
