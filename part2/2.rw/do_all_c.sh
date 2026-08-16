for file in *c.txt; do
    [ -f "$file" ] || continue
    ./process "$file" > "../3.time/$file"
done
