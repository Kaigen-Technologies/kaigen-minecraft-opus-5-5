@echo off
rem Opens the game at a reference scenario's camera: tools\scene.bat <1-20> [hud]
setlocal
set EXE=%~dp0..\out\windows\debug\minecraft-opus-5-5-kg-1.exe
set HUD=0.3:vx_hud:0;
if "%2"=="hud" set HUD=
set P=0.1:vx_seed:12345;0.2:vx_play;0.25:vx_spawn:natural=0;%HUD%
set GOLD=0.35:vx_give:item=gold_helmet;0.36:vx_equip:item=gold_helmet;0.37:vx_give:item=gold_chainmail;0.38:vx_equip:item=gold_chainmail;0.39:vx_give:item=gold_greaves;0.4:vx_equip:item=gold_greaves;
if "%1"=="1" set C=%P%0.5:vx_view:landmark=0 dist=170 up=82 around=0.35 time=0.36 rain=0 fly=1 pitch=-0.4
if "%1"=="2" set C=%P%0.5:vx_view:x=-75 z=66 above=48 yaw=5.44 pitch=-0.45 time=0.3 rain=0 fly=1
if "%1"=="3" set C=%P%0.5:vx_view:landmark=7 dist=60 up=70 around=2.75 pitch=-0.5 time=0.3 rain=0 fly=1
if "%1"=="4" set C=%P%%GOLD%0.41:vx_ability:wings=100;0.5:vx_view:x=372 z=-186 y=219 yaw=2.034 pitch=-0.2 cam=3 dist=7 time=0.3 rain=0 fly=1
if "%1"=="5" set C=%P%%GOLD%0.41:vx_ability:wings=100;0.5:vx_view:x=1.5 z=6 yaw=3.14159 pitch=-0.05 cam=3 dist=4.5 time=0.3 rain=0
if "%1"=="6" set C=%P%0.5:vx_view:x=10.2 z=-38.5 y=151.05 yaw=-1.5708 pitch=-0.02 cam=1 time=0.3 rain=0
if "%1"=="7" set C=%P%0.5:vx_view:x=37.5 z=16 yaw=2.75 pitch=-0.12 cam=3 dist=6.5 orbit=0.55 time=0.3 rain=0
if "%1"=="8" set C=%P%0.5:vx_view:x=-55 z=12 y=157 yaw=2.6 pitch=-0.55 cam=1 fly=1 time=0.3 rain=0
if "%1"=="9" set C=%P%%GOLD%0.41:vx_ability:wings=100;0.5:vx_view:x=67 z=-12.5 yaw=-2.0 pitch=-0.5 cam=3 dist=5 time=0.3 rain=0
if "%1"=="10" set C=%P%0.5:vx_view:landmark=0 dist=75 up=38 around=-0.5 fly=1 time=0.3 pitch=-0.35
if "%1"=="11" set C=%P%0.5:vx_view:x=-56 y=192 z=-162 yaw=-0.98 pitch=-0.3 fly=1 time=0.3 rain=0
if "%1"=="12" set C=%P%0.5:vx_view:landmark=7 dist=58 up=50 around=-0.4 fly=1 time=0.3 rain=0 pitch=-0.45
if "%1"=="13" set C=%P%0.5:vx_view:yaw=-1.5708 pitch=0.12 site=7 fly=1 time=0.3 rain=0
if "%1"=="14" set C=%P%0.5:vx_view:x=-66 y=84 z=62 yaw=2.1 pitch=-0.3 fly=1 time=0.3 rain=0
if "%1"=="15" set C=%P%0.5:vx_view:x=40.5 y=150 z=40.5 yaw=0.3 pitch=-1.45 fly=0 cam=3 dist=7
if "%1"=="16" set C=%P%0.5:vx_view:x=-43 y=21 z=-30 yaw=0 pitch=-0.15 fly=1 cam=3 dist=6
if "%1"=="17" set C=%P%0.5:vx_view:yaw=-1.5708 pitch=-0.12 site=8 cam=3 dist=4.5
if "%1"=="18" set C=%P%%GOLD%0.41:vx_ability:wings=1000;0.42:vx_give:item=enchanted_sword;0.43:vx_select:item=enchanted_sword;0.6:vx_view:x=-200.5 y=188.5 z=-30.5 yaw=-1.15 pitch=-0.18 fly=1 cam=3 dist=7 orbit=-0.45 time=0.53;4.5:vx_frame:x0=-206 z0=-38 x1=-170 z1=-22 y=185 h=6;5:vx_boss:kind=empress
if "%1"=="19" set C=%P%0.5:vx_view:x=70 z=-14 above=8 yaw=1.62 pitch=-0.55 fly=1 time=0.46 rain=0
if "%1"=="20" set C=0.1:vx_seed:12345
if not defined C (
  echo usage: tools\scene.bat ^<1-20^> [hud]   - reference frames: docs\terraria3d\scenes, side by side: out\cmp_S^<n^>.png
  exit /b 1
)
"%EXE%" --commands "%C%"
