pushd $2		# go to build folder

mysql -e "DROP DATABASE $1; CREATE DATABASE $1"
mysql -D "$1" -e "CREATE TABLE Mutants (MutantNR int NOT NULL, Killed bool DEFAULT FALSE, PRIMARY KEY(MutantNR)); CREATE TABLE Tests (Testname VARCHAR(1024), Testtime float DEFAULT 0.0, PRIMARY KEY(Testname));"

rm mutants.csv
#  [[ -n $line ]] prevents last line from being ignored if it doesn’t end in \n
cat list_of_tests.csv | while read line || [[ -n $line ]];
do
    # create table for the specific test
    mysql -D "$1" -e "CREATE TABLE \`$line\` (MutantNR int NOT NULL, Killed bool DEFAULT FALSE, Testtime float DEFAULT 0.0, PRIMARY KEY(MutantNR));"
    printf "running test: $line\n"
    export MUTANT_NR=0    # just to ensure its definitely 0 to export
    START_TOP=$(date +%s.%3N)
    $3 ${line%%,*}  #test cmd normally $3, but we only support ctest for now
    END_TOP=$(date +%s.%3N)
    DIFF_TOP=$(echo "$END_TOP - $START_TOP + 1" | bc)
    echo "INSERT INTO Tests VALUES (\`$line\`, $DIFF_TOP);"
    # all mutants are now stored in mutants.csv
    # we now put them into the database
    mysql -D "$1" -e "INSERT INTO Tests (Testname, Testtime) VALUES ('\`$line\`', '$DIFF_TOP'); LOAD DATA LOCAL INFILE 'mutants.csv' INTO TABLE \`$line\` LINES TERMINATED BY '\n'; INSERT IGNORE INTO Mutants SELECT * FROM \`$line\`;"
    #cat mutants.csv | while read line_mutant || [[ -n $line_mutant ]];
    #do
    #    mysql -D "$1" -e "INSERT INTO \`$line\` VALUES ($line_mutant, FALSE);"
    #done
    rm mutants.csv
done

#rm list_of_tests.csv
#rm ctest_info.json

popd
