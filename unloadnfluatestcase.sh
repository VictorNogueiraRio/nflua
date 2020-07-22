state=$1
function=$2

bash deliptablesrule.sh $state $function
bash unloadmodules.sh
