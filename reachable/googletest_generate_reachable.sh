pushd /opt/googletest/build/
ctest --show-only=json-v1 > ctest_info.json
jq --compact-output '.tests | .[] | .name' ctest_info.json > list_of_tests.csv
sed -i 's/\"//g' list_of_tests.csv
rm mutants.csv
popd


./reachable.sh googletest /opt/googletest/build/ "ctest -R "
