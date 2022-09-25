pushd /opt/json/build/
ctest --show-only=json-v1 > ctest_info.json
jq --compact-output '.tests | .[] | .name' ctest_info.json > list_of_tests.csv
sed -i 's/\"//g' list_of_tests.csv
rm mutants.csv
popd

./reachable.sh json /opt/json/build/ "ctest -R "
