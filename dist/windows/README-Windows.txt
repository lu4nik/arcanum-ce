Arcanum: Community Edition -- fork (lu4nik), Windows build
==========================================================
A modified build of Arcanum: Community Edition
(https://github.com/alexbatalov/arcanum-ce), free for non-commercial use under
the Sustainable Use License (licenses/arcanum-ce-LICENSE.md). Made with the
help of AI (Claude by Anthropic). Source: https://github.com/lu4nik/arcanum-ce
A fan project, not affiliated with the upstream project: please report
problems at https://github.com/lu4nik/arcanum-ua/issues, not upstream.

The fork is played and tested on a Mac; this Windows build is compiled by
GitHub Actions from the same source and has had little testing on Windows.
Feedback welcome. You need your own copy of the game (GOG or Steam).

1. Copy arcanum-ce.exe and the HighRes folder into the game folder (next to
   the original Arcanum.exe). Nothing of the original is replaced.
2. Start arcanum-ce.exe. The exe is not signed, so Windows SmartScreen may
   warn about it: "More info" -> "Run anyway".
3. Movies play through the game's own binkw32.dll (this is a 32-bit build
   for that reason; it runs on 64-bit Windows).

Works with the Unofficial Arcanum Patch 2.0.2 data (its exe patches are built
in; "UAP Rules" option in the video tab, on by default). If you change the
set of .dat files, delete data\proto once so the game rebuilds it.

Settings: HighRes\config.ini (or Options > Video in game); F9 cycles the
smoothing filters. For smoothing pick ScaleFX (needs Direct3D 12 or Vulkan;
restart the game once after choosing it); xBR/xBRZ run on the CPU and are
slow. Debug log: %APPDATA%\arcanum-ce\logs\arcanum-ce.log.

What the fork adds over upstream CE (see the repository for details):
- Engine bug fixes: 50 places where CE behaved differently from the original
  exe (dialogue conditions, script actions, combat criticals, AI, items,
  spells, reaction), plus fixes for bugs of the original game itself.
- Gamepad support (DualSense, DualShock 4, Xbox, Switch Pro, Steam Input) as
  a virtual mouse and keyboard; hold L1/L2/R1 or PS for the button chart.
- Automatic resolution and in-game Scale / Smoothing options; ScaleFX
  smoothing on the graphics card, xBR/xBRZ on the CPU.
- Autosave every 10 minutes into two rotating slots (F7 is never overwritten).
- Cyrillic (cp1251) text input; translated games keep NPC stats and genders.
- Ctrl+click moves items in loot, barter and steal windows.
