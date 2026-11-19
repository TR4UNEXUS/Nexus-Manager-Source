// SPDX-License-Identifier: GPL-3.0-or-later
namespace sphaira::ui {struct Sidebar:FormSidebar{using FormSidebar::FormSidebar;};}
namespace sphaira::devoptab {
namespace {
// OPEN_SAVED_PRODUCTION
}
// ADD_SHORTCUTS_PRODUCTION
}
using sphaira::ui::Sidebar;
Connection Smb(){return {"Test NAS","SMB","192.168.1.10","Games","testuser","WORKGROUP"};}
Connection Nfs(){return {"NFS library","NFS","192.168.1.11","/srv/games","ignored","ignored"};}
void Fresh(){Reset();state::locations.clear();state::loads=0;}
void Start(Sidebar& menu,const std::string& label){menu.Press(label);assert(!menu.pop);assert(App::progress);App::Run();assert(state::browses);}
int main(){
    Fresh();Sidebar empty("Install games");AddNetworkInstallShortcuts(empty);assert(empty.entries.empty());
    auto smb=Smb(),nfs=Nfs(),guest=smb;guest.name="Guest";guest.user.clear();guest.domain.clear();
    auto invalid=smb;invalid.name="Bad";invalid.host="smb://wrong";
    state::locations={smb,nfs,guest,invalid};Sidebar menu("Install games");AddNetworkInstallShortcuts(menu);assert(menu.entries.size()==3&&state::loads==2);
    // An authenticated shortcut asks only for a password and goes straight to
    // the connected browser. Its login is remembered immediately for restart.
    state::keyboard_text="session password";Start(menu,"[SMB] Test NAS");
    assert(state::keyboard_shows==1&&state::connected_url=="smb://192.168.1.10/Games"&&state::connected_password=="session password"&&state::saves==1);
    Start(menu,"[SMB] Test NAS");assert(state::keyboard_shows==1&&state::connected_password=="session password");
    assert(state::keyboard_header=="SMB password"&&state::keyboard_guide.find("Test NAS")!=std::string::npos&&state::keyboard_guide.find("testuser")!=std::string::npos);
    assert(state::keyboard_subtext.find("without encryption")!=std::string::npos);
    session_passwords.Clear();Start(menu,"[SMB] Test NAS");assert(state::keyboard_shows==1&&state::connected_password=="session password");
    Start(menu,"[NFS] NFS library");assert(state::keyboard_shows==1&&state::connected_password.empty()&&state::connected_url=="nfs://192.168.1.11/srv/games");
    Start(menu,"[SMB] Guest");assert(state::keyboard_shows==1&&state::connected_password.empty());
    session_passwords.Remember(guest,"explicit guest password");Start(menu,"[SMB] Guest");assert(state::keyboard_shows==1&&state::connected_password=="explicit guest password");
    // Re-read after list construction. A changed target must never receive
    // the password that was cached for the old server/account.
    state::locations[0].host="192.168.1.20";state::keyboard_text="new target password";Start(menu,"[SMB] Test NAS");
    assert(state::keyboard_shows==2&&state::connected_password=="new target password"&&state::connected_url=="smb://192.168.1.20/Games");
    state::locations.erase(state::locations.begin());const auto connected=state::connects;menu.Press("[SMB] Test NAS");assert(!App::progress&&state::connects==connected&&!state::error.empty());
    // Cancellation, applet mode and stale protocol entries never connect or
    // change saved settings. Explicit blank passwords remain a known choice.
    Fresh();state::locations={smb};Sidebar cancelled("Install games");AddNetworkInstallShortcuts(cancelled);
    state::keyboard_result=Cancelled;cancelled.Press("[SMB] Test NAS");assert(!App::progress&&state::connects==0&&state::saves==0&&state::locations[0].host==smb.host);
    state::applet=true;const auto keyboards=state::keyboard_shows;cancelled.Press("[SMB] Test NAS");assert(!App::progress&&state::keyboard_shows==keyboards&&state::connects==0);
    state::applet=false;state::keyboard_result=0;session_passwords.Remember(smb,"");Start(cancelled,"[SMB] Test NAS");assert(state::keyboard_shows==keyboards&&state::connected_password.empty());
    state::locations[0].protocol="NFS";state::locations[0].share="/srv/games";cancelled.Press("[SMB] Test NAS");assert(!App::progress);
    Fresh();state::locations={smb};session_passwords.Remember(smb,"failed password");Sidebar retry("Install games");AddNetworkInstallShortcuts(retry);
    state::connect_result=Fault;retry.Press("[SMB] Test NAS");App::Run();assert(state::keyboard_shows==0&&!state::error.empty());
    assert(App::form);state::connect_result=0;Start(retry,"[SMB] Test NAS");assert(state::keyboard_shows==0&&state::connected_password=="failed password");
    auto& retry_form=dynamic_cast<ConnectionForm&>(*App::form);Edit(retry_form,"Password","edited password");retry_form.Press("Browse SMB");App::Run();assert(state::keyboard_shows==1&&state::connected_password=="edited password");
    session_passwords.Clear();Start(retry,"[SMB] Test NAS");assert(state::keyboard_shows==1&&state::connected_password=="edited password");
    retry_form.Press("Remember password");std::string removed;assert(!ReadSavedPassword(state::locations.front(),removed));session_passwords.Clear();
    state::keyboard_text="session only";Start(retry,"[SMB] Test NAS");assert(state::keyboard_shows==2&&!ReadSavedPassword(state::locations.front(),removed)&&state::keyboard_subtext=="Used until Nexus Manager closes");
    // Every part of the target/account is part of the credential key. Friendly
    // names are labels only; a same-endpoint rename can reuse its session login.
    Fresh();session_passwords.Remember(smb,"only this endpoint");std::string password;
    auto renamed=smb;renamed.name="New label";assert(session_passwords.Find(renamed,password)&&password=="only this endpoint");
    for(int field=0;field<5;++field){auto changed=smb;if(field==0)changed.protocol="NFS";if(field==1)changed.host="another.local";if(field==2)changed.share="Other";if(field==3)changed.user="otheruser";if(field==4)changed.domain="OTHER";password="old";assert(!session_passwords.Find(changed,password)&&password.empty());}
    session_passwords.Forget(renamed);assert(!session_passwords.Find(smb,password));
    // The cache and emitted list are bounded even if a malformed caller feeds
    // more rows than the on-disk loader normally permits.
    for(unsigned i=0;i<65;++i){auto c=smb;c.host="server"+std::to_string(i);session_passwords.Remember(c,"password");}
    auto first=smb;first.host="server0";assert(!session_passwords.Find(first,password));auto last=smb;last.host="server64";assert(session_passwords.Find(last,password));
    for(unsigned i=0;i<100;++i){auto c=smb;c.name="NAS "+std::to_string(i);state::locations.push_back(c);}Sidebar bounded("Install games");AddNetworkInstallShortcuts(bounded);assert(bounded.entries.size()==64);
    // Saving/reopening a form remembers the typed password in this process;
    // editing an endpoint before saving does not copy that password to it.
    Fresh();ConnectionForm form(smb,true);Edit(form,"Password","form password");form.Press("Save location");assert(state::saves==1&&session_passwords.Find(smb,password)&&password=="form password");
    ConnectionForm reopened(smb,true);assert(Value(reopened,"Password")=="Entered");
    Edit(reopened,"Server","192.168.1.30");reopened.Press("Save location");auto changed=smb;changed.host="192.168.1.30";assert(!session_passwords.Find(changed,password));
    state::keyboard_text="changed server password";reopened.Press("Browse SMB");assert(state::keyboard_initial.empty());App::Run();assert(state::connected_password=="changed server password"&&session_passwords.Find(changed,password)&&password=="changed server password");
    Edit(reopened,"Password","");reopened.Press("Save location");assert(session_passwords.Find(changed,password)&&password.empty());
    ConnectionForm guest_form(guest,true);Edit(guest_form,"Password","entered guest password");guest_form.Press("Save location");guest_form.Press("Browse SMB");App::Run();assert(state::connected_password=="entered guest password");
    session_passwords.Clear();assert(!session_passwords.Find(changed,password));
    std::cout<<"Saved network shortcuts: direct SMB/NFS, restart credentials, labelled prompt, transient retry/edit, remember opt-out, endpoint binding, bounded rows, cancel and full-memory guards passed\n";
}
