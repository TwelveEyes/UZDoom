#!/usr/bin/env bash
# Ticket 05 spike (throwaway): every measurement arm on the stress vantages.
# Usage: spike05_runs.sh [classic]   (the classic arm is only re-run on request)
set -u
cd "$(dirname "$0")"
V="--demos demos/vantage --tics 1050 --backend vulkan"
arm() { local tag=$1; shift; ./run_matrix.sh $V --tag "$tag" "$@" | grep -E 'PASS|FAIL|matrix done'; }

if [ "${1:-}" = classic ]; then
	arm s05_classic --set "r_spike 0"
fi
for b in 64 128 256 512; do
	arm s05_hiz_b$b --set "r_spike 1" --set "r_spike_budget $b"
done
for b in 64 256; do
	arm s05_fr_b$b --set "r_spike 2" --set "r_spike_budget $b"
done
arm s05_nocull --set "r_spike 3"

# Synthetic shading cost (r_spike_fsload 256) on the heavy vantages.
HEAVY="--only SOS_MAP32_V1 --only PLANISF_MAP01_V1 --only MYHOUSE_MAP01_V1 --only PIRATES_MAP54_V1"
arm s05fs_hiz_b256 $HEAVY --set "r_spike 1" --set "r_spike_budget 256" --set "r_spike_fsload 256"
arm s05fs_fr_b256 $HEAVY --set "r_spike 2" --set "r_spike_budget 256" --set "r_spike_fsload 256"
arm s05fs_nocull $HEAVY --set "r_spike 3" --set "r_spike_fsload 256"

# Moving-sector vantages: perpetual lifts on the N sectors nearest the camera.
for mode in 0 1; do
	arm s05mov2000_m$mode --only SOS_MAP32_V2 --set "r_spike $mode" --set "r_spike_movers 2000"
	arm s05mov200_m$mode --only DOOM2_MAP27_V1 --set "r_spike $mode" --set "r_spike_movers 200"
done
echo ALL_DONE
