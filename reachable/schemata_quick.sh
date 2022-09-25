pushd $2		# go to build folder


#  [[ -n $line ]] prevents last line from being ignored if it doesn ^`^yt end in \n
cat list_of_tests.csv | while read line || [[ -n $line ]];
do
    printf "running test : $line\n"
    # -N skip column name, -s silent (removes ascci art)
    mysql -D "$1" -Nse "SELECT Mutants.MutantNR FROM Mutants INNER JOIN \`$line\` ON \`$line\`.MutantNR=Mutants.MutantNR WHERE Mutants.Killed = 'FALSE';" | while read line_mutant || [[ -n $line_mutant ]];
    do
        printf "running mutant: $line_mutant\n"
        export MUTANT_NR=$line_mutant
        (timeout -s SIGKILL $4s $3 $line) > ctest_mtlog.log 2>&1  #test cmd normally $3, but we only support ctest for now
        ret=$?
        #if grep -q '0 tests failed out of' ctest_mtlog.log; then
        if grep -q 'Tests failed: 0' ctest_mtlog.log; then
            echo "mutant survived"
        elif [[ ret == 137 ]]; then
            echo "UPDATE Mutants SET Killed = '1' WHERE MutantNR = $line_mutant;" >> $2updated_mutants.dbscr
            echo "UPDATE \`$line\` SET Killed = '1' WHERE MutantNR = $line_mutant;" >> $2updated_mutants.dbscr
            printf 'infinite loop, time out, mutant killed\n'
        else
            echo "UPDATE Mutants SET Killed = '1' WHERE MutantNR = $line_mutant;" >> $2updated_mutants.dbscr
            echo "UPDATE \`$line\` SET Killed = '1' WHERE MutantNR = $line_mutant;" >> $2updated_mutants.dbscr
        fi
        rm ctest_mtlog.log
    done
    cat $2updated_mutants.dbscr
    mysql -D "$1" -e "`cat $2updated_mutants.dbscr`"
    rm $2updated_mutants.dbscr
    #break
done

#rm list_of_tests.csv
#rm ctest_info.json

#popd
