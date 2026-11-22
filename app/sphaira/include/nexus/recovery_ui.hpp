// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/localize.hpp"
#include "nexus/index_recovery.hpp"

namespace sphaira::nexusapp {
// Presentation only: never infer a successful check from a successful scan.
inline std::string RecoverySummary(const RecoveryPlan& p, Result result, bool cancelled=false) {
    std::string text;
    if(R_FAILED(result)) {
        if(p.published)
            text="Repair did not finish\n\nClose USB Loader and fully restart the Switch. Then open Help & options > Repair USB library before installing or moving games. Keep all repair backups."_nx;
        else if(result==nexus::registration::RepairPending)
            text="An earlier library change needs attention\n\nFully restart the Switch. If a USB import was interrupted, open Help & options > Import USB games into this system and restore its backup. Otherwise use Repair USB library to restore the repair backup first."_nx;
        else
            text=std::string(cancelled?"Check cancelled"_nx:"Check could not finish"_nx)+
                "\n\nNo live library entries or game files were changed. Check that the SD and USB drive are connected, close other installers, then try again. Details has more information."_nx;
    } else if(p.pending) {
        text="Backup restored\n\nThe previous USB library was restored. Close USB Loader and fully restart the Switch, then run Repair USB library again. Game files and saves were not changed."_nx;
    } else if(p.mismatch_files || p.skipped || p.blocked_records) {
        text="Some content needs attention\n\n"_nx;
        if(p.mismatch_files)text+="Some files did not match their expected contents. Modified files can also cause this. These files were not changed.\n\n"_nx;
        else text+="Some content could not be fully checked. It may be missing, incomplete or conflict with another copy.\n\n"_nx;
        text+="Open Details to review the findings. Missing or damaged content needs reinstalling from your known-good backup. Saves are unchanged."_nx;
        if(p.published)text+="\n\nOther verified entries were repaired. Close the app and fully restart the Switch before continuing."_nx;
    } else if(p.verification_complete && p.repair_complete) {
        if(p.published)
            text="Repair complete\n\nMissing USB library entries were repaired after checking their game files. Game files and saves were not changed.\n\nClose USB Loader and fully restart the Switch before playing."_nx;
        else
            text="Check complete\n\nThe available game files passed the check. No library repairs were needed.\n\nIf the game still will not start, it may need reinstalling from your original backup. This check does not reset the system's corruption warning."_nx;
    } else if(!p.only_application && p.intact && !p.ready) {
        text="No missing library entries found\n\nThis checks the USB game list, not the contents of every game.\n\nTo fully check a game's available files, select it in My games and choose Check and repair."_nx;
    } else {
        text="No repair available from this system's records\n\nThis does not mean the USB drive is empty. Games installed from another SD card, emuMMC or sysMMC may not be registered here.\n\nUse Help & options > Import USB games into this system to scan the drive and preview those games. Missing files or tickets still require your original backup. Nothing was changed."_nx;
    }
    if(!p.scan_records.empty()){
        std::set<u64> games,affected;
        for(const auto& record:p.scan_records)games.insert(record.application);
        for(const auto& m:p.metadata)if(m.blocked)affected.insert(m.application_id?m.application_id:m.key.id);
        text+="\n\nGames reviewed: "_nx+std::to_string(games.size());
        if(!affected.empty())text+="\nGames with mismatched files: "_nx+std::to_string(affected.size())+"\n"+"Updates and DLC are counted under their game."_nx;
    }
    if(p.backup_attempted)text+="\n\n"+p.backup_detail;
    if(p.verification_started)
        text+=p.report_path.empty()?"\n\nThe detailed report could not be saved. Details is still available here."_nx:"\n\nA detailed report was saved to SD."_nx;
    return text;
}
inline std::string RecoveryConfirmation(bool selected) {
    return std::string(selected?"Check and repair this USB game?"_nx:"Repair the USB game library?"_nx)+
        (selected?"\n\nCheck the available game, update and extra-content files, then repair any missing library entries automatically."_nx:
                  "\n\nLook for missing library entries and check the affected files before repairing them. Games already listed are not fully rechecked; use My games > Check and repair for that."_nx)+
        "\n\nGame files and saves are kept. Missing or damaged files need reinstalling from your own backup. This does not reset the system's corruption warning.\n\nClose other games and installers. Keep the SD and USB drive connected until finished."_nx;
}
}
