# usage: ./tradMutate.sh csvFile "./a.out" projectLocation "projectBuildCmd" "testCmd" timeout

rruntime=0.0
bbuildtime=0.0
THEexitCMD=0
cat $1 | while read line || [[ -n $line ]];
do
    THEexitCMD=0
    echo $line
    $2 $line	# ./mutateSpecificFile args
    pushd $3	# go to build folder
    start=0
    if $4; then		# build cmd
        start=$(date +%s.%N)
        timeout -s SIGKILL $6s $5	#test cmd
        THEexitCMD=$?
        if [[ THEexitCMD == 137 ]]; then
            printf 'infinite loop, time out, mutant killed\n'
        fi
        end=$(date +%s.%N)
        local_runtime=$(echo "$end - $start" | bc)
        echo "local runtime:"
        echo $local_runtime
        rruntime=$(echo "$rruntime + $local_runtime" | bc)
        echo "total runtime so far:"
        echo $rruntime
    else
        printf 'failed to build, no need to test\n'
    fi
    end=$(date +%s.%N)
    local_buildtime=$(echo "$end - $start" | bc)
    bbuildtime=$(echo "$rruntime + $local_runtime" | bc)
    git reset --hard	# cleanup any changes
    popd	# go back to original folder
    printf "exit: $THEexitCMD " >> mt_progress.txt
    printf "$line\n" >> mt_progress.txt
    printf $rruntime >> mt_progress.txt
    printf "n" >> mt_progress.txt
done
echo "total tuntime: "
echo $rruntime
echo "tottal buildtime: "
echo $bbuildtime
