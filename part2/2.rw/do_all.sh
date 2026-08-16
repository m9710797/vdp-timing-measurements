for file in *.txt; do
    [ -f "$file" ] || continue
    ./process "$file" > "../3.time/$file"
done
