# Control Flow

### 9.1 If Expression/Statement

`if` can be used as an expression (returns value) or statement (for effects).

```endo
# If as EXPRESSION (returns value, requires else)
let status = if count > 0 then "has items" else "empty"
let max = if a > b then a else b
let sign = if n < 0 then -1 elif n > 0 then 1 else 0

# Multi-line if expression
let category =
    if age < 13 then
        "child"
    elif age < 20 then
        "teenager"
    elif age < 65 then
        "adult"
    else
        "senior"

# If as STATEMENT (for side effects, else optional)
if fileExists path then
    echo "File found"
    cat $path
fi

if hasErrors then
    echo "Errors detected!"
    exit 1
fi

# With else
if fileExists path then
    echo "File found"
    cat $path
else
    echo "File not found"
    exit 1
fi

# Elif chains
if status == 200 then
    echo "OK"
elif status == 404 then
    echo "Not found"
elif status >= 500 then
    echo "Server error"
else
    echo "Unknown: $status"
fi

# Single-line (semicolons optional because 'then' is keyword)
if test -f $file then cat $file fi
if test -f $file then cat $file else echo "missing" fi

# Parentheses optional around condition
if (count > 0) then process fi
if count > 0 then process fi        # Same thing
```

### 9.2 Match Expression

See [Pattern Matching](pattern-matching.md) for comprehensive coverage.

```endo
# Match as expression
let description =
    match status with
    | 200 -> "OK"
    | 404 -> "Not Found"
    | code when code >= 500 -> "Server Error"
    | _ -> "Unknown"

# Match as statement
match command with
| "start" -> startServer
| "stop" -> stopServer
| "status" -> showStatus
| cmd -> echo "Unknown command: $cmd"
```

### 9.3 Loops

```endo
# For-in loop over list
for item in [1; 2; 3; 4; 5] do
    echo "Item: $item"
done

# For-in over range
for i in 1..10 do
    echo "Count: $i"
done

for i in 10..-1..1 do
    echo "Countdown: $i"
done

# For-in over command output
for file in $(ls *.txt) do
    echo "Processing: $file"
    wc -l $file
done

# For-in with destructuring
for (name, value) in entries do
    echo "$name = $value"
done

for { host; port } in servers do
    ping $host
done

# While loop
let mut n = 10
while n > 0 do
    echo "Countdown: $n"
    n <- n - 1
done

# Infinite loop with break
while true do
    let input = read
    if input == "quit" then break fi
    process input
done

# Break and continue
for item in items do
    if item == "skip" then continue fi
    if item == "stop" then break fi
    process item
done

# While with complex condition
while hasMoreData && !cancelled do
    processNextBatch
done
```

### 9.4 Bash-Compatible Control Flow

For compatibility, traditional bash syntax is also supported.

```endo
# Traditional if-then-else-fi with test
if [ -f "$file" ]; then
    cat "$file"
elif [ -d "$file" ]; then
    ls "$file"
else
    echo "Not found"
fi

# Brackets optional with endo conditions
if test -f $file then
    cat $file
fi

# Case statement (bash style)
case $command in
    start)
        start_service
        ;;
    stop)
        stop_service
        ;;
    restart)
        stop_service
        start_service
        ;;
    *)
        echo "Unknown command"
        ;;
esac

# Short-circuit operators as control flow
test -f $file && cat $file
test -f $file || echo "Not found"
command1 && command2 || fallback

# C-style for loop (coming soon)
# for ((i = 0; i < 10; i++)) do
#     echo $i
# done
```

---
**See also:** [Pattern Matching](pattern-matching.md) | [Error Handling](error-handling.md) | [Command Execution](command-execution.md)
