#include "SpoolmanDialog.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <tuple>
#include <utility>

#include <boost/algorithm/string.hpp>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MsgDialog.hpp"
#include "BitmapCache.hpp"
#include "Widgets/ComboBox.hpp"
#include "libslic3r/Color.hpp"
#include "filamentsync/FilamentAssignment.hpp"

namespace Slic3r { namespace GUI {

// ---------------------------------------------------------------------------------------------
// Connection settings
// ---------------------------------------------------------------------------------------------

SpoolmanDialog::SpoolmanDialog(wxWindow *parent, const SpoolmanSettings &s)
    : wxDialog(parent, wxID_ANY, _L("Spoolman settings"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    , m_settings(s)
{
    auto *grid = new wxFlexGridSizer(2, wxSize(8, 8));
    grid->AddGrowableCol(1, 1);

    auto add_row = [&](const wxString &label, wxWindow *ctrl) {
        grid->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(ctrl, 1, wxEXPAND);
    };

    m_host = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(s.host.c_str()), wxDefaultPosition, wxSize(FromDIP(280), -1));
    m_host->SetToolTip(_L("Host name or IP address of the Spoolman server, e.g. spoolman.example.com"));
    // A fresh configuration starts from the port Spoolman listens on by default.
    const std::string port = (s.host.empty() && s.port.empty()) ? std::string("7912") : s.port;
    m_port                 = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(port.c_str()));
    m_port->SetToolTip(_L("Spoolman listens on port 7912 by default. Leave empty to use the default port of the protocol."));
    m_https = new wxCheckBox(this, wxID_ANY, _L("Use HTTPS"));
    m_https->SetValue(s.use_https);
    m_username = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(s.username.c_str()));
    m_username->SetToolTip(_L("Optional. Only needed when the Spoolman server sits behind a proxy asking for HTTP basic authentication."));
    m_password = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(s.password.c_str()), wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
    m_password->SetToolTip(_L("Optional. Only needed when the Spoolman server sits behind a proxy asking for HTTP basic authentication."));

    add_row(_L("Host"), m_host);
    add_row(_L("Port"), m_port);
    add_row(wxEmptyString, m_https);
    add_row(_L("User name"), m_username);
    add_row(_L("Password"), m_password);

    auto *test_btn = new wxButton(this, wxID_ANY, _L("Test connection"));
    test_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { on_test(); });

    auto *buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->Add(test_btn, 0, wxRIGHT, FromDIP(8));
    buttons->AddStretchSpacer(1);
    buttons->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0);

    auto *top = new wxBoxSizer(wxVERTICAL);
    top->Add(grid, 1, wxEXPAND | wxALL, FromDIP(12));
    top->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
    SetSizerAndFit(top);
    wxGetApp().UpdateDlgDarkUI(this);
    CentreOnParent();
}

SpoolmanSettings SpoolmanDialog::get_settings() const
{
    // Trim: a stray space or newline pasted with the host name turns into an unresolvable host.
    auto val = [](const wxTextCtrl *c) { return std::string(c->GetValue().Trim(true).Trim(false).ToUTF8().data()); };
    SpoolmanSettings s = m_settings; // keeps hide_empty, which this dialog does not show
    s.host      = val(m_host);
    s.port      = val(m_port);
    s.use_https = m_https->GetValue();
    s.username  = val(m_username);
    s.password  = val(m_password);
    return s;
}

void SpoolmanDialog::on_test()
{
    const SpoolmanSettings s = get_settings();
    if (!s.valid()) {
        show_error(this, _L("Please fill in the host of the Spoolman server."));
        return;
    }
    Spoolman client(s);
    wxString msg;
    bool     ok;
    {
        wxBusyCursor wait;
        ok = client.test(msg);
    }
    if (ok)
        show_info(this, msg, _L("Spoolman"));
    else
        show_error(this, msg);
}

// ---------------------------------------------------------------------------------------------
// Spool selection
// ---------------------------------------------------------------------------------------------

namespace {

std::string lower(const std::string &s) { return boost::algorithm::to_lower_copy(s); }

// Grouped by material, then vendor, then name: the spools of one material sit together, which is
// how they get picked for a project.
bool spool_order(const SpoolmanSpool &a, const SpoolmanSpool &b)
{
    return std::make_tuple(lower(a.material), lower(a.vendor), lower(a.filament_name), a.id) <
           std::make_tuple(lower(b.material), lower(b.vendor), lower(b.filament_name), b.id);
}

wxString remaining_label(const SpoolmanSpool &s)
{
    if (!s.has_remaining_weight())
        return wxString::FromUTF8("\xE2\x80\x94"); // em dash
    return wxString::Format("%.0f g", s.remaining_weight);
}

} // namespace

SpoolmanSpoolDialog::SpoolmanSpoolDialog(wxWindow *parent, std::vector<SpoolmanSpool> spools, size_t max_slots, bool hide_empty)
    : wxDialog(parent, wxID_ANY, _L("Spoolman spools"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_spools(std::move(spools))
    , m_max_slots(std::max<size_t>(max_slots, 1))
{
    std::sort(m_spools.begin(), m_spools.end(), spool_order);

    auto *top = new wxBoxSizer(wxVERTICAL);

    auto *intro = new wxStaticText(this, wxID_ANY,
                                   _L("Tick the spools to use in this project. They replace the filament list of the "
                                      "project: one filament slot per spool, in the order they are ticked. To keep "
                                      "the slots the project already has, and only choose which spool goes on each "
                                      "of them, use \"Match with currents\" instead."));
    intro->Wrap(FromDIP(520));
    top->Add(intro, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    // Filter row
    auto *filter_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_filter           = new wxTextCtrl(this, wxID_ANY, wxEmptyString);
    m_filter->SetHint(_L("Filter by name, material or location"));
    m_filter->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { apply_filter(); });
    m_hide_empty = new wxCheckBox(this, wxID_ANY, _L("Hide empty spools"));
    m_hide_empty->SetValue(hide_empty);
    m_hide_empty->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) { apply_filter(); });
    filter_sizer->Add(m_filter, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    filter_sizer->Add(m_hide_empty, 0, wxALIGN_CENTER_VERTICAL);
    top->Add(filter_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    // Spool list
    m_list = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(520), FromDIP(360)), wxVSCROLL | wxBORDER_SIMPLE);
    m_list->SetScrollRate(0, FromDIP(10));
    auto *list_sizer = new wxBoxSizer(wxVERTICAL);

    m_rows.reserve(m_spools.size());
    for (size_t i = 0; i < m_spools.size(); ++i) {
        const SpoolmanSpool &spool = m_spools[i];

        Row row;
        row.spool_index = i;
        row.panel       = new wxPanel(m_list, wxID_ANY);
        auto *row_sizer = new wxBoxSizer(wxHORIZONTAL);

        row.slot = new wxStaticText(row.panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(26), -1), wxALIGN_RIGHT);
        row_sizer->Add(row.slot, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));

        auto *swatch = new wxPanel(row.panel, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(14), FromDIP(14)), wxBORDER_SIMPLE);
        const std::string color = spool.primary_color();
        if (!color.empty()) {
            const wxColour c(wxString::FromUTF8(color.c_str()));
            if (c.IsOk())
                swatch->SetBackgroundColour(c);
        }
        swatch->SetToolTip(color.empty() ? _L("This spool has no color in Spoolman.") : wxString::FromUTF8(color.c_str()));
        row_sizer->Add(swatch, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

        row.check = new wxCheckBox(row.panel, wxID_ANY, wxString::FromUTF8(spool.display_name().c_str()));
        wxString tip = wxString::Format(_L("Spool #%d in Spoolman"), spool.id);
        if (!spool.location.empty())
            tip += "\n" + wxString::Format(_L("Location: %s"), wxString::FromUTF8(spool.location.c_str()));
        if (!spool.lot_nr.empty())
            tip += "\n" + wxString::Format(_L("Lot: %s"), wxString::FromUTF8(spool.lot_nr.c_str()));
        row.check->SetToolTip(tip);
        const size_t row_index = m_rows.size();
        row.check->Bind(wxEVT_CHECKBOX, [this, row_index](wxCommandEvent &) { on_toggle(row_index); });
        row_sizer->Add(row.check, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

        auto *material = new wxStaticText(row.panel, wxID_ANY, wxString::FromUTF8(spool.material.c_str()), wxDefaultPosition,
                                          wxSize(FromDIP(70), -1));
        row_sizer->Add(material, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

        auto *remaining = new wxStaticText(row.panel, wxID_ANY, remaining_label(spool), wxDefaultPosition, wxSize(FromDIP(64), -1),
                                           wxALIGN_RIGHT);
        remaining->SetToolTip(_L("Filament left on the spool, as Spoolman knows it."));
        row_sizer->Add(remaining, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

        row.panel->SetSizer(row_sizer);
        list_sizer->Add(row.panel, 0, wxEXPAND | wxALL, FromDIP(3));
        m_rows.push_back(row);
    }

    m_list->SetSizer(list_sizer);
    top->Add(m_list, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    auto *match_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto *match_btn   = new wxButton(this, ID_MATCH_CURRENT, _L("Match with currents..."));
    match_btn->SetToolTip(_L("Keep the filament slots of the project and pick, for each one, the spool closest to "
                             "the color it was designed with. The number of slots does not change, so the filament "
                             "assigned to every part stays as it is."));
    match_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { EndModal(ID_MATCH_CURRENT); });
    match_sizer->Add(match_btn, 0);
    top->Add(match_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    m_summary = new wxStaticText(this, wxID_ANY, wxEmptyString);
    top->Add(m_summary, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    top->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, FromDIP(12));
    if (auto *ok_btn = FindWindow(wxID_OK))
        ok_btn->SetLabel(_L("Assign to project"));

    SetSizerAndFit(top);
    SetMinSize(GetSize());
    wxGetApp().UpdateDlgDarkUI(this);
    apply_filter();
    update_slots();
    CentreOnParent();
}

bool SpoolmanSpoolDialog::matches_filter(const SpoolmanSpool &spool) const
{
    if (m_hide_empty != nullptr && m_hide_empty->GetValue() && spool.has_remaining_weight() && spool.remaining_weight <= 0.)
        return false;

    const std::string needle = lower(std::string(m_filter->GetValue().ToUTF8().data()));
    if (needle.empty())
        return true;
    const std::string haystack = lower(spool.display_name() + " " + spool.material + " " + spool.location + " " + spool.lot_nr);
    return haystack.find(needle) != std::string::npos;
}

void SpoolmanSpoolDialog::apply_filter()
{
    for (const Row &row : m_rows)
        row.panel->Show(matches_filter(m_spools[row.spool_index]));
    m_list->Layout();
    m_list->FitInside();
}

void SpoolmanSpoolDialog::on_toggle(size_t row_index)
{
    if (row_index >= m_rows.size())
        return;
    const Row   &row         = m_rows[row_index];
    const size_t spool_index = row.spool_index;

    if (row.check->GetValue()) {
        if (m_selection.size() >= m_max_slots) {
            // The project cannot hold more filaments than the printer has slots.
            row.check->SetValue(false);
            m_summary->SetLabel(wxString::Format(_L("At most %d spools can be assigned to the project."), int(m_max_slots)));
            m_summary->SetForegroundColour(wxColour(0xC0, 0x39, 0x2B));
            Layout();
            return;
        }
        if (std::find(m_selection.begin(), m_selection.end(), spool_index) == m_selection.end())
            m_selection.push_back(spool_index);
    } else {
        m_selection.erase(std::remove(m_selection.begin(), m_selection.end(), spool_index), m_selection.end());
    }
    update_slots();
}

void SpoolmanSpoolDialog::update_slots()
{
    for (const Row &row : m_rows) {
        const auto it = std::find(m_selection.begin(), m_selection.end(), row.spool_index);
        row.slot->SetLabel(it == m_selection.end() ?
                               wxString() :
                               wxString::Format("%d.", int(std::distance(m_selection.begin(), it)) + 1));
    }
    m_summary->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
    m_summary->SetLabel(m_selection.empty()
                            ? _L("No spool selected.")
                            : wxString::Format(_L("%d of at most %d spools selected."), int(m_selection.size()), int(m_max_slots)));
    if (auto *ok_btn = FindWindow(wxID_OK))
        ok_btn->Enable(!m_selection.empty());
    Layout();
}

std::vector<SpoolmanSpool> SpoolmanSpoolDialog::selected_spools() const
{
    std::vector<SpoolmanSpool> out;
    out.reserve(m_selection.size());
    for (const size_t index : m_selection)
        if (index < m_spools.size())
            out.push_back(m_spools[index]);
    return out;
}

bool SpoolmanSpoolDialog::hide_empty() const { return m_hide_empty != nullptr && m_hide_empty->GetValue(); }

// ---------------------------------------------------------------------------------------------
// Assignment of the spools to the filament slots of the project
// ---------------------------------------------------------------------------------------------

namespace {

// How a CIEDE2000 distance reads to the eye.  The thresholds are the usual ones: ~1 invisible,
// ~2 just noticeable, ~5 clearly another shade, beyond 10 another color.  Both sides of the
// comparison are nominal values typed into a profile or an inventory rather than measurements,
// so the ranking is what matters here, not the absolute figure.
wxColour assign_delta_colour(float delta)
{
    if (delta < 0.f)
        return wxColour(0x80, 0x80, 0x80);
    if (delta <= 2.f)
        return wxColour(0x27, 0xAE, 0x60);
    if (delta <= 5.f)
        return wxColour(0xB8, 0x8A, 0x00);
    if (delta <= 10.f)
        return wxColour(0xD3, 0x5A, 0x00);
    return wxColour(0xC0, 0x39, 0x2B);
}

wxString assign_delta_text(float delta)
{
    if (delta < 0.f)
        return wxString::FromUTF8("\xE2\x80\x94"); // em dash: no color to compare
    return wxString::Format(wxString::FromUTF8("\xCE\x94" "E %.1f"), delta);
}

// The spool color as a small solid bar, shown next to each entry of the drop-down and on the
// closed control.  A border keeps a white or very light spool visible against the background.
wxBitmap assign_color_bitmap(wxWindow *context, const std::string &hex)
{
    ColorRGB rgb;
    if (context == nullptr || hex.empty() || !decode_color(hex, rgb))
        return wxNullBitmap;
    BitmapCache cache;
    return cache.mksolid(size_t(context->FromDIP(26)), size_t(context->FromDIP(14)), rgb, true, 1,
                         wxGetApp().dark_mode());
}

wxString assign_choice_label(const SpoolmanSpool &spool, float delta)
{
    const wxString separator = wxString::FromUTF8("  \xC2\xB7  "); // middle dot
    wxString       label     = assign_delta_text(delta) + separator + wxString::FromUTF8(spool.display_name().c_str());
    if (!spool.material.empty())
        label += separator + wxString::FromUTF8(spool.material.c_str());
    if (spool.has_remaining_weight())
        label += separator + wxString::Format("%.0f g", spool.remaining_weight);
    return label;
}

} // namespace

SpoolmanAssignDialog::SpoolmanAssignDialog(wxWindow *parent, std::vector<SpoolmanSpool> spools, std::vector<Slot> slots, bool hide_empty)
    : wxDialog(parent, wxID_ANY, _L("Assign Spoolman spools to the filament slots"), wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_spools(std::move(spools))
    , m_slots(std::move(slots))
{
    std::sort(m_spools.begin(), m_spools.end(), spool_order);
    m_assignment.assign(m_slots.size(), -1);

    auto *top = new wxBoxSizer(wxVERTICAL);

    auto *intro = new wxStaticText(this, wxID_ANY,
                                   _L("Each row is a filament slot of the project, with the color it was designed "
                                      "with. Pick the spool to load on it, or let \"Best match\" choose the "
                                      "combination with the smallest total color difference. Pinned rows are kept "
                                      "as they are."));
    intro->Wrap(FromDIP(660));
    top->Add(intro, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    // Filters: they decide which spools the rows offer, and so what "Best match" may choose.
    auto *filter_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_filter           = new wxTextCtrl(this, wxID_ANY, wxEmptyString);
    m_filter->SetHint(_L("Filter by name, material or location"));
    m_filter->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { rebuild_choices(); });
    m_hide_empty = new wxCheckBox(this, wxID_ANY, _L("Hide empty spools"));
    m_hide_empty->SetValue(hide_empty);
    m_hide_empty->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) { rebuild_choices(); });
    filter_sizer->Add(m_filter, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    filter_sizer->Add(m_hide_empty, 0, wxALIGN_CENTER_VERTICAL);
    top->Add(filter_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    // One row per filament slot of the project.
    const int visible_rows = int(std::min<size_t>(std::max<size_t>(m_slots.size(), 1), 8));
    m_list = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(660), FromDIP(42 * visible_rows)),
                                  wxVSCROLL | wxBORDER_SIMPLE);
    m_list->SetScrollRate(0, FromDIP(10));
    auto *list_sizer = new wxBoxSizer(wxVERTICAL);

    m_rows.reserve(m_slots.size());
    for (size_t i = 0; i < m_slots.size(); ++i) {
        const Slot &slot = m_slots[i];

        Row  row;
        row.panel      = new wxPanel(m_list, wxID_ANY);
        auto *row_sizer = new wxBoxSizer(wxHORIZONTAL);

        auto *number = new wxStaticText(row.panel, wxID_ANY, wxString::Format("%d.", int(i) + 1), wxDefaultPosition,
                                        wxSize(FromDIP(26), -1), wxALIGN_RIGHT);
        row_sizer->Add(number, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));

        // The design color: the reference every distance on this row is measured from.
        row.swatch = new wxPanel(row.panel, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(16), FromDIP(16)), wxBORDER_SIMPLE);
        {
            const wxColour colour(wxString::FromUTF8(slot.design_color.c_str()));
            if (colour.IsOk())
                row.swatch->SetBackgroundColour(colour);
            wxString tip = slot.name.empty() ? wxString() : wxString::FromUTF8(slot.name.c_str()) + "\n";
            tip += _L("Design color:") + " " +
                   (slot.design_color.empty() ? _L("unknown") : wxString::FromUTF8(slot.design_color.c_str()));
            if (!slot.current_color.empty() && slot.current_color != slot.design_color)
                tip += "\n" + _L("Loaded now:") + " " + wxString::FromUTF8(slot.current_color.c_str());
            row.swatch->SetToolTip(tip);
        }
        row_sizer->Add(row.swatch, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

        auto *type = new wxStaticText(row.panel, wxID_ANY, wxString::FromUTF8(slot.type.c_str()), wxDefaultPosition,
                                      wxSize(FromDIP(54), -1));
        row_sizer->Add(type, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

        row.choice = new ComboBox(row.panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(380), -1), 0,
                                  nullptr, wxCB_READONLY);
        row.choice->Bind(wxEVT_COMBOBOX, [this, i](wxCommandEvent &) {
            update_row_delta(i);
            update_summary();
        });
        row_sizer->Add(row.choice, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

        row.delta = new wxStaticText(row.panel, wxID_ANY, assign_delta_text(-1.f), wxDefaultPosition,
                                     wxSize(FromDIP(62), -1));
        row_sizer->Add(row.delta, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

        row.pin = new wxCheckBox(row.panel, wxID_ANY, _L("Pin"));
        row.pin->SetToolTip(_L("Keep this row as it is: \"Best match\" assigns the other slots around it. "
                               "This is how a slot is held on a material of its own."));
        row_sizer->Add(row.pin, 0, wxALIGN_CENTER_VERTICAL);

        row.panel->SetSizer(row_sizer);
        list_sizer->Add(row.panel, 0, wxEXPAND | wxALL, FromDIP(3));
        m_rows.push_back(row);
    }

    m_list->SetSizer(list_sizer);
    top->Add(m_list, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    // Actions
    auto *action_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto *best_btn     = new wxButton(this, wxID_ANY, _L("Best match"));
    best_btn->SetToolTip(_L("Assign the spools to all the slots at once, minimizing the total color difference. "
                            "One spool per slot."));
    best_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { on_best_match(); });
    auto *clear_btn = new wxButton(this, wxID_ANY, _L("Clear"));
    clear_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { clear_assignment(); });
    m_keep_material = new wxCheckBox(this, wxID_ANY, _L("Best match keeps the material"));
    m_keep_material->SetValue(true);
    m_keep_material->SetToolTip(_L("When ticked, \"Best match\" never puts a spool of another material on a slot. "
                                   "A spool chosen by hand, or a pinned row, is not affected."));
    action_sizer->Add(best_btn, 0, wxRIGHT, FromDIP(8));
    action_sizer->Add(clear_btn, 0, wxRIGHT, FromDIP(12));
    action_sizer->Add(m_keep_material, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
    m_use_loaded = new wxCheckBox(this, wxID_ANY, _L("Compare with the colors loaded now"));
    m_use_loaded->SetToolTip(_L("By default a spool is compared with the color its slot was designed with. Tick this "
                                "when that reference is not meaningful -- a project whose palette was never set -- to "
                                "compare with the color the slot carries right now instead."));
    m_use_loaded->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) { rebuild_choices(); });
    action_sizer->Add(m_use_loaded, 0, wxALIGN_CENTER_VERTICAL);
    top->Add(action_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    m_summary = new wxStaticText(this, wxID_ANY, wxEmptyString);
    top->Add(m_summary, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    top->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, FromDIP(12));
    if (auto *ok_btn = FindWindow(wxID_OK))
        ok_btn->SetLabel(_L("Assign to project"));

    Bind(wxEVT_BUTTON, [this](wxCommandEvent &event) {
        if (event.GetId() != wxID_OK) {
            event.Skip();
            return;
        }
        m_assignment.assign(m_slots.size(), -1);
        for (size_t i = 0; i < m_rows.size(); ++i)
            m_assignment[i] = selected_spool(i);
        event.Skip();
    });

    SetSizerAndFit(top);
    SetMinSize(GetSize());
    wxGetApp().UpdateDlgDarkUI(this);
    rebuild_choices();
    CentreOnParent();
}

bool SpoolmanAssignDialog::hide_empty() const { return m_hide_empty != nullptr && m_hide_empty->GetValue(); }

const std::string &SpoolmanAssignDialog::reference_color(size_t slot_index) const
{
    static const std::string empty;
    if (slot_index >= m_slots.size())
        return empty;
    const Slot &slot = m_slots[slot_index];
    if (m_use_loaded != nullptr && m_use_loaded->GetValue() && !slot.current_color.empty())
        return slot.current_color;
    return slot.design_color.empty() ? slot.current_color : slot.design_color;
}

bool SpoolmanAssignDialog::spool_offered(const SpoolmanSpool &spool) const
{
    if (m_hide_empty != nullptr && m_hide_empty->GetValue() && spool.has_remaining_weight() && spool.remaining_weight <= 0.)
        return false;

    const std::string needle = lower(std::string(m_filter->GetValue().ToUTF8().data()));
    if (needle.empty())
        return true;
    const std::string haystack = lower(spool.display_name() + " " + spool.material + " " + spool.location + " " + spool.lot_nr);
    return haystack.find(needle) != std::string::npos;
}

int SpoolmanAssignDialog::selected_spool(size_t row_index) const
{
    if (row_index >= m_rows.size())
        return -1;
    const Row &row      = m_rows[row_index];
    const int  position = row.choice->GetSelection();
    if (position <= 0 || size_t(position) > row.offered.size())
        return -1; // position 0 is "leave unchanged"
    return row.offered[size_t(position) - 1];
}

void SpoolmanAssignDialog::select_spool(size_t row_index, int spool_index)
{
    if (row_index >= m_rows.size())
        return;
    Row &row = m_rows[row_index];
    if (spool_index < 0) {
        row.choice->SetSelection(0);
    } else {
        const auto it = std::find(row.offered.begin(), row.offered.end(), spool_index);
        row.choice->SetSelection(it == row.offered.end() ? 0 : int(std::distance(row.offered.begin(), it)) + 1);
    }
    update_row_delta(row_index);
}

void SpoolmanAssignDialog::rebuild_choices()
{
    for (size_t i = 0; i < m_rows.size(); ++i) {
        Row               &row       = m_rows[i];
        const int          kept      = selected_spool(i);
        const std::string &reference = reference_color(i);

        // The spools this slot may take, closest color first: with seven slots to fill, the
        // ordering is what makes the right spool findable without reading the whole inventory.
        std::vector<std::pair<float, int>> ranked;
        ranked.reserve(m_spools.size());
        for (size_t j = 0; j < m_spools.size(); ++j) {
            if (!spool_offered(m_spools[j]) && int(j) != kept)
                continue;
            const float delta = color_delta_e(reference, m_spools[j].primary_color());
            // An unknown distance sorts last, never first.
            ranked.emplace_back(delta < 0.f ? std::numeric_limits<float>::max() : delta, int(j));
        }
        std::stable_sort(ranked.begin(), ranked.end(),
                         [](const std::pair<float, int> &a, const std::pair<float, int> &b) { return a.first < b.first; });

        // The swatch is the reference the row is measured against, so it follows the toggle.
        if (row.swatch != nullptr) {
            const wxColour colour(wxString::FromUTF8(reference.c_str()));
            if (colour.IsOk())
                row.swatch->SetBackgroundColour(colour);
            row.swatch->Refresh();
        }

        row.choice->Clear();
        row.offered.clear();
        row.choice->Append(_L("Leave unchanged"));
        for (const std::pair<float, int> &entry : ranked) {
            const SpoolmanSpool &spool = m_spools[size_t(entry.second)];
            const float          delta = color_delta_e(reference, spool.primary_color());
            row.choice->Append(assign_choice_label(spool, delta),
                               assign_color_bitmap(row.choice, spool.primary_color()));
            row.offered.push_back(entry.second);
        }
        select_spool(i, kept);
    }
    update_summary();
    m_list->Layout();
    m_list->FitInside();
}

void SpoolmanAssignDialog::update_row_delta(size_t row_index)
{
    if (row_index >= m_rows.size())
        return;
    Row        &row   = m_rows[row_index];
    const int   spool = selected_spool(row_index);
    const float delta = spool < 0 ? -1.f :
                                    color_delta_e(reference_color(row_index), m_spools[size_t(spool)].primary_color());
    row.delta->SetLabel(assign_delta_text(delta));
    row.delta->SetForegroundColour(assign_delta_colour(delta));
    row.delta->Refresh();
}

void SpoolmanAssignDialog::clear_assignment()
{
    for (size_t i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].pin->GetValue())
            continue;
        select_spool(i, -1);
    }
    update_summary();
}

void SpoolmanAssignDialog::on_best_match()
{
    // The candidates are the spools the filters currently offer, plus the ones pinned rows hold:
    // a pin is the user's decision and must survive whatever the filter hides.
    std::vector<int> pool;
    for (size_t j = 0; j < m_spools.size(); ++j)
        if (spool_offered(m_spools[j]))
            pool.push_back(int(j));
    for (size_t i = 0; i < m_rows.size(); ++i) {
        if (!m_rows[i].pin->GetValue())
            continue;
        const int pinned = selected_spool(i);
        if (pinned >= 0 && std::find(pool.begin(), pool.end(), pinned) == pool.end())
            pool.push_back(pinned);
    }
    if (pool.empty()) {
        m_summary->SetForegroundColour(wxColour(0xC0, 0x39, 0x2B));
        m_summary->SetLabel(_L("No spool passes the filter, so there is nothing to match."));
        Layout();
        return;
    }

    std::vector<AssignmentCandidate> candidates;
    candidates.reserve(pool.size());
    for (const int j : pool) {
        const SpoolmanSpool &spool = m_spools[size_t(j)];
        AssignmentCandidate  candidate;
        candidate.color              = spool.primary_color();
        candidate.type               = spool.material;
        candidate.available_weight   = spool.has_remaining_weight() ? spool.remaining_weight : -1.;
        candidates.push_back(candidate);
    }

    std::vector<AssignmentSlot> slots;
    slots.reserve(m_slots.size());
    for (size_t i = 0; i < m_slots.size(); ++i) {
        AssignmentSlot slot;
        slot.design_color    = reference_color(i);
        slot.type            = m_slots[i].type;
        slot.required_weight = m_slots[i].required_weight;
        if (m_rows[i].pin->GetValue()) {
            const int pinned = selected_spool(i);
            if (pinned < 0) {
                slot.skipped = true; // pinned on "leave unchanged": keep it that way
            } else {
                const auto it = std::find(pool.begin(), pool.end(), pinned);
                slot.pinned   = it == pool.end() ? -1 : int(std::distance(pool.begin(), it));
            }
        }
        slots.push_back(slot);
    }

    AssignmentOptions options;
    options.forbid_type_mismatch = m_keep_material != nullptr && m_keep_material->GetValue();
    options.exclusive            = true;

    const std::vector<int> result = compute_optimal_assignment(slots, candidates, options);
    for (size_t i = 0; i < result.size() && i < m_rows.size(); ++i) {
        if (m_rows[i].pin->GetValue())
            continue;
        select_spool(i, result[i] < 0 ? -1 : pool[size_t(result[i])]);
    }
    update_summary();
}

void SpoolmanAssignDialog::update_summary()
{
    size_t assigned = 0;
    float  worst    = -1.f;
    std::map<int, int> usage;
    for (size_t i = 0; i < m_rows.size(); ++i) {
        const int spool = selected_spool(i);
        if (spool < 0)
            continue;
        ++assigned;
        ++usage[spool];
        const float delta = color_delta_e(reference_color(i), m_spools[size_t(spool)].primary_color());
        if (delta > worst)
            worst = delta;
    }

    int shared = 0;
    for (const std::pair<const int, int> &entry : usage)
        if (entry.second > 1)
            ++shared;

    wxString text;
    if (assigned == 0) {
        text = _L("No slot assigned yet.");
    } else {
        text = wxString::Format(_L("%d of %d slots assigned."), int(assigned), int(m_slots.size()));
        if (worst >= 0.f)
            text += " " + wxString::Format(_L("Largest difference on a slot: %s."), assign_delta_text(worst));
    }
    if (shared > 0)
        text += " " + _L("A spool is assigned to more than one slot.");

    m_summary->SetForegroundColour(shared > 0 ? wxColour(0xB8, 0x8A, 0x00) :
                                                wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
    m_summary->SetLabel(text);
    Layout();
}

}} // namespace Slic3r::GUI
