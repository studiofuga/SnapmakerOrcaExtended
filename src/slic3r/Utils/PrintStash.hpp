#ifndef slic3r_PrintStash_hpp_
#define slic3r_PrintStash_hpp_

#include <string>
#include <boost/filesystem/path.hpp>
#include "nlohmann/json.hpp"
#include "PrintHost.hpp"

namespace Slic3r {

class AppConfig;

// User settings of the PrintStash integration, stored in the (untracked) application config.
struct PrintStashSettings
{
    std::string host;        // bare host name / IP, or a full URL ("https://printstash.example.com")
    std::string port;        // optional
    bool        use_https{false};
    std::string username;
    std::string api_key;
    std::string collection;  // optional, e.g. "Functional/Brackets"
    std::string tags;        // optional, comma separated

    static PrintStashSettings load(const AppConfig &config);
    void                      save(AppConfig &config) const;

    // host, username and API key are mandatory
    bool        valid() const;
    std::string base_url() const;
};

// Native port of tools/printstash_orca_push.py: logs into a PrintStash server with an API key and
// pushes the sliced G-code together with the metadata extracted from the G-code itself.
class PrintStash : public PrintHost
{
public:
    explicit PrintStash(PrintStashSettings settings);
    ~PrintStash() override = default;

    const char *get_name() const override;
    bool        test(wxString &curl_msg) const override;
    wxString    get_test_ok_msg() const override;
    wxString    get_test_failed_msg(wxString &msg) const override;
    bool        upload(PrintHostUpload upload_data, ProgressFn progress_fn, ErrorFn error_fn, InfoFn info_fn) const override;
    bool        has_auto_discovery() const override { return false; }
    bool        can_test() const override { return true; }
    PrintHostPostUploadActions get_post_upload_actions() const override { return PrintHostPostUploadAction::None; }
    std::string get_host() const override { return m_settings.base_url(); }

    // Parses the G-code (header comments, object labels, config block, ";printstash:key=value" markers)
    // into the "native_context" JSON document expected by POST /api/v1/ingest/orca.
    static nlohmann::json extract_native_context(const boost::filesystem::path &gcode);

private:
    bool login(std::string &token, wxString &error) const;

    PrintStashSettings m_settings;
};

} // namespace Slic3r

#endif
