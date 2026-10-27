#ifndef slic3r_GUI_SpoolmanDialog_hpp_
#define slic3r_GUI_SpoolmanDialog_hpp_

#include <vector>

#include <wx/dialog.h>

#include "slic3r/Utils/Spoolman.hpp"

class wxCheckBox;
class ComboBox;
class wxPanel;
class wxScrolledWindow;
class wxStaticText;
class wxTextCtrl;

namespace Slic3r { namespace GUI {

// Connection settings of the Spoolman integration (host, port, optional basic auth).
class SpoolmanDialog : public wxDialog
{
public:
    SpoolmanDialog(wxWindow *parent, const SpoolmanSettings &settings);

    SpoolmanSettings get_settings() const;

private:
    void on_test();

    SpoolmanSettings m_settings; // the settings the dialog was opened with (keeps the fields it does not show)

    wxTextCtrl *m_host;
    wxTextCtrl *m_port;
    wxCheckBox *m_https;
    wxTextCtrl *m_username;
    wxTextCtrl *m_password;
};

// Picks the spools of the Spoolman inventory to assign to the project: the selection replaces the
// filament list of the project, one filament slot per spool, in the order the spools were ticked.
class SpoolmanSpoolDialog : public wxDialog
{
public:
    // Ends the dialog when the user would rather assign spools to the slots the project already
    // has than rebuild the filament list from the inventory.
    enum : int { ID_MATCH_CURRENT = wxID_HIGHEST + 300 };

    SpoolmanSpoolDialog(wxWindow *parent, std::vector<SpoolmanSpool> spools, size_t max_slots, bool hide_empty);

    // The ticked spools, in the order they were ticked.
    std::vector<SpoolmanSpool> selected_spools() const;
    // State of the "Hide empty spools" filter, to be remembered in the settings.
    bool hide_empty() const;

private:
    struct Row
    {
        size_t        spool_index{0};
        wxPanel      *panel{nullptr};
        wxCheckBox   *check{nullptr};
        wxStaticText *slot{nullptr};
    };

    void on_toggle(size_t row_index);
    void apply_filter();
    void update_slots();       // the "1.", "2." ... labels and the summary line
    bool matches_filter(const SpoolmanSpool &spool) const;

    std::vector<SpoolmanSpool> m_spools;   // sorted by material, vendor and name
    std::vector<Row>           m_rows;     // one per spool, same order as m_spools
    std::vector<size_t>        m_selection; // spool indices, in the order they were ticked
    size_t                     m_max_slots{1};

    wxTextCtrl       *m_filter{nullptr};
    wxCheckBox       *m_hide_empty{nullptr};
    wxScrolledWindow *m_list{nullptr};
    wxStaticText     *m_summary{nullptr};
};

// Assigns the spools of the Spoolman inventory to the filament slots the project ALREADY has: one
// row per slot, so every slot keeps its index and the filament assignment of the parts is left
// alone.  Each row shows how far a spool is from the color its slot was designed with (CIEDE2000,
// lower is closer), and "Best match" solves all the rows at once rather than slot by slot: picking
// greedily would let an early slot take the spool a later one needed much more.
class SpoolmanAssignDialog : public wxDialog
{
public:
    // One filament slot of the project.
    struct Slot
    {
        std::string design_color;        // "#RRGGBB": the color the project was designed with
        std::string current_color;       // the color loaded on the slot right now
        std::string type;                // "PLA", "PETG", ...
        std::string name;                // profile label, shown in the row tooltip
        double      required_weight{0.}; // grams the slot needs; 0 when the project is not sliced
    };

    SpoolmanAssignDialog(wxWindow *parent, std::vector<SpoolmanSpool> spools, std::vector<Slot> slots, bool hide_empty);

    // The spool assigned to each slot, as an index into spools(); -1 leaves that slot untouched.
    const std::vector<int> &assignment() const { return m_assignment; }
    // The spools in the order this dialog sorted them; assignment() indexes into this.
    const std::vector<SpoolmanSpool> &spools() const { return m_spools; }
    // State of the "Hide empty spools" filter, to be remembered in the settings.
    bool hide_empty() const;

private:
    struct Row
    {
        wxPanel         *panel{nullptr};
        wxPanel         *swatch{nullptr};
        ComboBox        *choice{nullptr};
        wxStaticText    *delta{nullptr};
        wxCheckBox      *pin{nullptr};
        std::vector<int> offered; // choice position (1-based) -> index into m_spools
    };

    // The color a row's distances are measured from: the design color, or -- when the project's
    // design palette is just a placeholder, as it is for a list built slot by slot in the slicer --
    // the color loaded right now.
    const std::string &reference_color(size_t slot_index) const;

    void rebuild_choices();   // refills every row, ordering the spools by distance from that slot
    void on_best_match();
    void clear_assignment();
    void update_row_delta(size_t row_index);
    void update_summary();
    int  selected_spool(size_t row_index) const;
    void select_spool(size_t row_index, int spool_index);
    bool spool_offered(const SpoolmanSpool &spool) const;

    std::vector<SpoolmanSpool> m_spools;     // sorted by material, vendor and name
    std::vector<Slot>          m_slots;
    std::vector<Row>           m_rows;       // one per slot, same order as m_slots
    std::vector<int>           m_assignment; // filled when the dialog is accepted

    wxTextCtrl       *m_filter{nullptr};
    wxCheckBox       *m_hide_empty{nullptr};
    wxCheckBox       *m_keep_material{nullptr};
    wxCheckBox       *m_use_loaded{nullptr};
    wxScrolledWindow *m_list{nullptr};
    wxStaticText     *m_summary{nullptr};
};

}} // namespace Slic3r::GUI

#endif
