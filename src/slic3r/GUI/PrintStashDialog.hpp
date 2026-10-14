#ifndef slic3r_GUI_PrintStashDialog_hpp_
#define slic3r_GUI_PrintStashDialog_hpp_

#include <wx/dialog.h>
#include "slic3r/Utils/PrintStash.hpp"

class wxTextCtrl;
class wxCheckBox;

namespace Slic3r { namespace GUI {

// Connection settings of the PrintStash integration (host, port, user name, API key, ...).
class PrintStashDialog : public wxDialog
{
public:
    PrintStashDialog(wxWindow *parent, const PrintStashSettings &settings);

    PrintStashSettings get_settings() const;

private:
    void on_test();

    wxTextCtrl *m_host;
    wxTextCtrl *m_port;
    wxCheckBox *m_https;
    wxTextCtrl *m_username;
    wxTextCtrl *m_api_key;
    wxTextCtrl *m_collection;
    wxTextCtrl *m_tags;
};

}} // namespace Slic3r::GUI

#endif
