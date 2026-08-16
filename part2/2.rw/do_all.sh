for file in *.txt; do
    [ -f "$file" ] || continue
    ./process "$file" > "../time/$file"
done
