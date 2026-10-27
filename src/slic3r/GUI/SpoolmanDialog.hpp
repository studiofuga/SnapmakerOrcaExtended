#ifndef slic3r_GUI_SpoolmanDialog_hpp_
#define slic3r_GUI_SpoolmanDialog_hpp_

#include <vector>

#include <wx/dialog.h>

#include "slic3r/Utils/Spoolman.hpp"

class wxCheckBox;
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

}} // namespace Slic3r::GUI

#endif
