// SPDX-License-Identifier: GPL-3.0-or-later
// Original Nexus menu pictograms. Unit canvas: 96 x 96.
// One geometry definition feeds NanoVG and the host SVG preview.
#pragma once
#include <initializer_list>
namespace sphaira::ui::nexusview {
enum class MenuIcon { Installed, Sd, Usb, Cleanup, Homebrew, Mods, Folder,
                      HomeLibrary, HomeInstall, FileManager, More,
                      MorePc, MoreCartridge, MoreSave, MoreHelp };
struct Point { float x,y; };
template<class Painter> void PaintIcon(Painter& p,MenuIcon icon) {
    switch(icon) {
    case MenuIcon::MorePc:
        // A desktop screen with transfer arrows and a stable monitor stand.
        p.Rect(10,14,76,52,6);
        p.Path({{10,56},{86,56}},false);
        p.Path({{39,67},{37,80},{59,80},{57,67}},false);
        p.Path({{28,83},{68,83}},false);
        p.Path({{26,29},{67,29},{60,22}},false);
        p.Path({{67,29},{60,36}},false);
        p.Path({{68,44},{27,44},{34,37}},false);
        p.Path({{27,44},{34,51}},false);
        break;
    case MenuIcon::MoreCartridge:
        // Distinct game-card shape, recessed game label and contact row.
        p.Path({{29,10},{64,10},{76,22},{76,83},{20,83},{20,19}},true);
        p.Rect(28,26,40,30,4);
        p.Path({{35,40},{47,40}},false);
        p.Path({{41,34},{41,46}},false);
        p.Circle(57,37,2.5f);p.Circle(61,45,2.5f);
        for(int x:{30,39,48,57,66})p.Path({{float(x),67},{float(x),76}},false);
        break;
    case MenuIcon::MoreSave:
        // Save disk with a second copy behind it and a completed check badge.
        p.Path({{27,10},{73,10},{86,23},{86,69}},false);
        p.Path({{15,20},{60,20},{74,34},{74,60}},false);
        p.Path({{74,82},{15,82},{15,20}},false);
        p.Rect(27,20,27,22,2);
        p.Path({{46,25},{46,36}},false);
        p.Path({{26,81},{26,56},{57,56},{57,64}},false);
        p.Circle(72,74,16);
        p.Path({{64,74},{70,80},{81,68}},false);
        break;
    case MenuIcon::MoreHelp:
        // Lifebuoy: help and recovery with four clear grip sections.
        p.Circle(48,48,35);p.Circle(48,48,18);
        p.Path({{24,23},{35,35}},false);
        p.Path({{33,17},{41,31}},false);
        p.Path({{63,17},{55,31}},false);
        p.Path({{72,23},{61,35}},false);
        p.Path({{72,73},{61,61}},false);
        p.Path({{63,79},{55,65}},false);
        p.Path({{33,79},{41,65}},false);
        p.Path({{24,73},{35,61}},false);
        break;
    case MenuIcon::HomeLibrary:
        // Offset game cases, open behind the foreground controller. Their
        // outlines stop at the overlap so the silhouette remains readable.
        p.Path({{25,13},{69,13},{78,22},{78,42}},false);
        p.Path({{20,19},{64,19},{72,27},{72,43}},false);
        p.Path({{27,79},{12,79},{12,27},{59,27},{65,33},{65,44}},false);
        p.Path({{22,27},{22,79}},false);
        p.Rect(31,36,22,9,2);
        p.Path({{36,50},{72,50},{80,56},{88,78},{85,84},{77,84},{65,74},
                {43,74},{32,84},{25,84},{22,78},{29,56}},true);
        p.Path({{33,62},{47,62}},false);
        p.Path({{40,55},{40,69}},false);
        p.Circle(66,58,2.5f);p.Circle(75,66,2.5f);
        p.Path({{53,67},{58,67}},false);
        break;
    case MenuIcon::HomeInstall:
        // A broad download arrow entering a recessed storage tray. This does
        // not resemble the SD-only source icon used by the file browser.
        p.Path({{42,12},{54,12},{54,34},{65,34},{48,51},{31,34},{42,34}},true);
        p.Path({{19,54},{24,43},{29,43}},false);
        p.Path({{67,43},{72,43},{77,54}},false);
        p.Path({{14,55},{34,55},{40,64},{56,64},{62,55},{82,55},
                {82,80},{77,85},{19,85},{14,80}},true);
        p.Path({{26,75},{57,75}},false);
        p.Circle(70,75,2.5f);
        break;
    case MenuIcon::FileManager:
        // Folder, folded document and two opposed arrows: a file action rather
        // than a storage device. Arrowheads remain distinct at small sizes.
        p.Path({{11,39},{11,24},{34,24},{43,33},{81,33},{81,39}},false);
        p.Path({{45,32},{45,11},{64,11},{76,23},{76,32}},false);
        p.Path({{64,11},{64,23},{76,23}},false);
        p.Path({{52,29},{67,29}},false);
        p.Path({{11,41},{86,41},{79,83},{17,83}},true);
        p.Path({{29,55},{65,55}},false);
        p.Path({{58,48},{65,55},{58,62}},false);
        p.Path({{65,70},{29,70}},false);
        p.Path({{36,63},{29,70},{36,77}},false);
        break;
    case MenuIcon::More:
        // A consistent tool grid with a larger gear in the last position.
        p.Rect(12,12,27,27,5);
        p.Path({{19,25},{32,25}},false);
        p.Path({{25.5f,18.5f},{25.5f,31.5f}},false);
        p.Rect(52,12,31,24,5);
        p.Path({{58,24},{63,24}},false);
        p.Circle(67,24,4);
        p.Path({{71,24},{77,24}},false);
        p.Rect(12,53,24,31,5);
        p.Path({{19,63},{29,63}},false);
        p.Path({{19,73},{29,73}},false);
        p.Path({{61,43},{71,43},{73,49},{78,51},{84,49},{89,58},
                {84,63},{84,69},{89,74},{84,83},{78,81},{73,83},
                {71,89},{61,89},{59,83},{54,81},{48,83},{43,74},
                {48,69},{48,63},{43,58},{48,49},{54,51},{59,49}},true);
        p.Circle(66,66,9);
        break;
    case MenuIcon::Folder:
        p.Path({{12,40},{12,23},{38,23},{48,33},{80,33},{80,40}},false);
        p.Path({{12,40},{86,40},{78,78},{18,78}},true);
        break;
    case MenuIcon::Mods:
        // Layered game data and sliders: optional, reviewed customisations.
        p.Path({{12,25},{48,10},{84,25},{48,40}},true);
        p.Path({{12,36},{48,51},{84,36}},false);
        p.Path({{12,48},{48,63},{84,48}},false);
        p.Path({{18,74},{78,74}},false);
        p.Rect(31,68,8,12,3);
        p.Path({{18,88},{78,88}},false);
        p.Rect(60,82,8,12,3);
        break;
    case MenuIcon::Homebrew:
        // Handheld game screen with play arrow: launch, not install.
        p.Rect(10,18,76,62,12);
        p.Rect(29,28,38,37,5);
        p.Path({{43,36},{56,46},{43,56}},true);
        p.Path({{15,46},{25,46}},false);
        p.Path({{20,41},{20,51}},false);
        p.Circle(75,41,2);p.Circle(75,52,2);
        p.Path({{40,73},{55,73}},false);
        break;
    case MenuIcon::Installed:
        // A library of game cases, with a controller on the front case.
        p.Path({{25,12},{72,12},{78,18}},false);
        p.Path({{17,21},{66,21},{74,29}},false);
        p.Rect(13,30,70,54,10);
        p.Path({{24,57},{40,57}},false);
        p.Path({{32,49},{32,65}},false);
        p.Circle(59,53,3);
        p.Circle(70,63,3);
        p.Path({{42,74},{54,74}},false);
        break;
    case MenuIcon::Sd:
        // Clipped SD-card shoulder and four contact fingers.
        p.Path({{34,11},{72,11},{79,18},{79,84},{18,84},{18,30}},true);
        p.Rect(29,49,39,24,4);
        for(int x:{40,49,58,67})p.Path({{float(x),19},{float(x),35}},false);
        p.Path({{24,29},{24,40}},false);
        break;
    case MenuIcon::Usb:
        // USB-A flash drive with trident on its body.
        p.Rect(32,10,32,23,3);
        p.Rect(23,32,50,53,9);
        p.Path({{42,16},{42,24}},false);
        p.Path({{54,16},{54,24}},false);
        p.Path({{48,71},{48,44}},false);
        p.Path({{43,49},{48,44},{53,49}},false);
        p.Path({{48,64},{38,57},{38,52}},false);
        p.Circle(38,50,2);
        p.Path({{48,59},{58,53},{58,49}},false);
        p.Rect(56,44,4,4,0);
        p.Circle(48,73,3);
        break;
    case MenuIcon::Cleanup:
        // Broom + sparkle: reviewed unfinished files, not a format command.
        p.Path({{49,47},{74,17},{80,22},{56,52}},true);
        p.Path({{29,43},{61,61},{43,83},{13,65}},true);
        p.Path({{24,66},{35,53}},false);
        p.Path({{34,72},{44,59}},false);
        p.Path({{43,77},{52,65}},false);
        p.Path({{24,12},{27,22},{37,25},{27,28},{24,38},{21,28},{11,25},{21,22}},true);
        p.Path({{78,57},{78,71}},false);
        p.Path({{71,64},{85,64}},false);
        break;
    }
}
}
