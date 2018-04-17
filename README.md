# simple-lua

A compiler which compiles (a subset of) lua to machine code.

## Status

- [x] Lexer
- [x] Parser
- [x] Code emitter
- [x] If statement
- [x] Number-based for statement
- [ ] Range-based for statement
- [ ] While statement
- [ ] Repeat statement
- [ ] Break statement
- [ ] Function
- [ ] Local variable
- [x] Table array part
- [ ] Table hash part
- [x] Table indexing
- [ ] Table field access
- [x] Integer literal
- [x] Floating point literal
- [ ] String literal
- [ ] Function closure
- [ ] Operator overloading

## Building

### Requirements

- LLVM 5.0.1 or later
- CMake 3.10 or later
- C++ compiler with C++17 support
- Git

### Steps

```
git clone --recursive https://github.com/Moandor-y/simple-lua.git
cd slua/
mkdir build
cd build/
cmake -DCMAKE_BUILD_TYPE=Release ..
make slua slua_rt
```

### Example

Compile the following program which finds the k-th smallest divisor of n

```
n = 866421317361600
sqrt_n = 29435035
k = 26878

divisors = {}

for i = 1, sqrt_n do
  if n % i == 0 then
    divisors[#divisors + 1] = i
  end
end

if k <= #divisors then
  print(divisors[k])
else
  if divisors[#divisors] * divisors[#divisors] == n then
    if k <= #divisors * 2 - 1 then
      print(n // divisors[#divisors - (k - #divisors)])
    else
      print(0)
    end
  else
    if k <= #divisors * 2 then
      print(n // divisors[#divisors - (k - #divisors) + 1])
    else
      print(0)
    end
  end
end
```

Save the above program in a file named `in.lua`

Compile it with slua

```
./slua
```

Link the generated object file with runtime library

```
g++ output.o libslua_rt.a
```

Execute the program

```
./a.out
```

And it prints the correct answer `288807105787200`
