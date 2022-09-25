pushd $2    # go to build folder
cat $1 | while read line || [[ -n $line ]];
do
    printf "executing mutant: ${line%%,*}\n"
    export MUTANT_NR="${line%%,*}"
    timeout -s SIGKILL $4s $3   #test cmd
    if [[ $? == 137 ]]; then
        printf 'infinite loop, time out, mutant killed\n'
    fi
done
popd       # go back to original folder

