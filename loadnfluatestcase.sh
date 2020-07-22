state=$1
script=$2
function=$3
bash loadmodules.sh
bash createluastate.sh $state
bash loadluascript.sh $state $script
bash addiptablesrule.sh $state $function
