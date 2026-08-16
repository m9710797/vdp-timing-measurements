for path in scr5-dispOff-*c.txt; do
    grep -v "^\( 285\| 413\| 541\| 669\| 797\| 925\|1053\|1181\|1237\|1245\|1253\|1261\)" $path > ../5.slots/${path}
done
