#!/bin/bash
# captures look-dev postcards (TERRARIA_3D_PLAN.md §4) into out/postcards/<platform>/<name>.png
# usage: tools/postcards.sh native|wasm P1 P2 ...   (seed via VX_SEED, settle time via VX_SETTLE)
set -u
cd "$(dirname "$0")/.." || exit 1
platform=$1
shift
seed=${VX_SEED:-12345}
settle=${VX_SETTLE:-7}
out=out/postcards/$platform
mkdir -p "$out"
status=0
# commands a postcard needs before its shot, as <seconds before the shot>:<command>, separated by ;
pre_shot() {
  case $1 in
  P11) echo "9:vx_spawn:natural=0;8.5:vx_fill:x0=-10 y0=185 z0=12 x1=90 y1=185 z1=19 block=Wood_Platform;8:vx_view:x=20.5 y=186.3 z=15.5 yaw=-1.57 pitch=0.2 fly=0 cam=3 dist=10 orbit=0.45;7.8:vx_give:item=Gold_Broadsword;7.6:vx_select:item=Gold_Broadsword;7.4:vx_give:item=Iron_Helmet equip=1;7.3:vx_give:item=Iron_Chainmail equip=1;7.2:vx_give:item=Iron_Greaves equip=1;4:vx_boss" ;;
  P12) echo "1.65:vx_spawn:kind=zombie x=18.5 z=11.5;1.6:vx_spawn:kind=zombie x=21.5 z=5.5;1.55:vx_spawn:kind=zombie x=16.5 z=7.5;1.5:vx_spawn:kind=eye x=20.5 z=8.5 y=156;1.45:vx_spawn:kind=slime x=23.5 z=9.5;0.15:vx_use" ;;
  P13) echo "1.75:vx_give:item=gold_broadsword count=1 slot=4;1.65:vx_select:item=gold_broadsword;0.75:vx_spawn:kind=zombie x=26.0 z=14.0 hp=1;0.7:vx_spawn:kind=zombie x=25.2 z=14.9;0.65:vx_spawn:kind=slime x=26.2 z=13.2;0.2:vx_use" ;;
  esac
}
# the pre-shot commands placed before a shot at time $2
pre_cmds() {
  pre_shot "$1" | tr ';' '\n' | while IFS= read -r c; do
    [ -n "$c" ] && printf ';%s:%s' "$(awk "BEGIN{print $2 - ${c%%:*}}")" "${c#*:}"
  done
}
if [ "$platform" = native ]; then
  cmds="0.1:vx_seed:$seed;0.2:vx_play"
  t=0.5
  for p in "$@"; do
    cmds="$cmds;$t:vx_view:$p"
    t=$(awk "BEGIN{print $t + $settle}")
    cmds="$cmds$(pre_cmds "$p" "$t");$t:screenshot:$out/$p.png"
    t=$(awk "BEGIN{print $t + 0.5}")
  done
  cmds="$cmds;$t:quit"
  ./hz/hzbuild.exe --commands "$cmds" > "$out/run.log" 2>&1 || status=1
  grep -aiE " error|assert|panic" "$out/run.log" && status=1
else
  for p in "$@"; do
    rm -f out/screenshot_wasm.png
    ./hz/hzbuild.exe wasm --release --commands "0.1:vx_seed:$seed;0.2:vx_play;0.5:vx_view:$p$(pre_cmds "$p" "$settle");$settle:screenshot;$(awk "BEGIN{print $settle + 1}"):quit" > "$out/$p.log" 2>&1 || status=1
    grep -aiE "error|assert|panic" "$out/$p.log" | grep -v " 404 " && status=1
    cp out/screenshot_wasm.png "$out/$p.png" || status=1
  done
fi
ls -l "$out"/*.png
exit $status
