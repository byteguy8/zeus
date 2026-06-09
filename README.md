# Zeus Programming Language
The toy programming language Zeus (I now, ironic).

## Build
### Linux

```
cd zeus
mkdir build
make BUILD=RELEASE PLATFORM=LINUX
```

### Windows

```
cd zeus
mkdir build
make BUILD=RELEASE PLATFORM=WINDOWS
```

## Run
### Linux

```
./build/zeus /path/to/source/file
```

### Windows (PowerShell)

```
./build/zeus.exe /path/to/source/file
```

## Examples
### Values

```
empty;                   // equivalent of 'nil' in others languages
false;                   // booleans
9223372036854775807;     // 64 bits integers
3.142857143;             // 64 bits doubles
"Hi!"                    // immutable strings
array(0,1,2,4,5,6,7,8,9) // arrays
list(0,1,2,4,5,6,7,8,9)  // dynamic arrays
dict(                    // dictionaries
    "name" to "Zeus",
    "age" to 37
);
```

### Variables
    let mut foo = false; // can change its value
    let bar = 3.14; // cannot change its value

### Strings

Strings in Zeus are immutable

#### Indexing

```
let foo = "Hello world!";

println(foo[0]);          // printing first character
println(foo[foo.len() - 1]);  // printing last character
```

#### Concatenation

```
let name = "Michael";
let lastname = "Byte";
let full_name = name .. " " .. lastname; // result: 'Michael Byte'
```

#### Multiplication

```
let foo = "ha" ** 8; // result: hahahahahahahaha
```

### Data Structures
#### Arrays

You can create a empty array of x length:

```
let values = array[1024];
println(values.len());
```

or just hardcode some values:

```
let foo = array(empty, false, true, 2, 3.14);

println(foo[0]);         // getting the value at index 0
foo[0] = "Some text..."; // replacing value at index 0
println(foo[0]);         // printing new value at index 0
```
#### Lists

```
let bar = list(empty, false, true, 2, 3.14);

println(foo[0]);           // getting value at index 0
foo[0] = "Some text...";   // setting value at index 0
```

#### Dicts

```
let foo = dict(
    1 to "I will not waste chalk",
    2 to "I will not skateboard in the halls",
    3 to "I will not instigate revolution"
);

println(foo[2]);                // getting the value at key '2'
foo[4] = "I did not see Elvis"; // replacing the value at key '4'
```

### try/catch
You can use the throw statement with a value:

```
proc foo(){
    throw "error from foo"; // throw with value
}
```

or without:

```
proc bar(){
    throw; // throw without any value
}
```

If throw statement is used with a value, that value will be in the declared variable after the 'catch' keyword:

```
try{
    foo();
}catch err{
    println(err); // it prints 'error from foo'
}
```

otherwise, the value of the declared value will be 'empty'

```
try{
    bar();
}catch err{
    println(err); // it prints 'empty'
}
```

But you can also use catch without a variable, even if throw is used with some value

```
try{
    foo();
}catch{
    // some code...
}
```

### Conditional

```
let mut sum0 = 0;
let mut sum1 = 0;
let mut sum2 = 0;
let mut sum3 = 0;

for(let i = 1; i <= 100; i += 1){
    if(i < 25){
        sum0 += i;
    }elif(i < 50){
        sum1 += i;
    }elif(i < 75){
        sum2 += i;
    }else{
        sum3 += i;
    }
}

sum1 += sum0;
sum2 += sum1;
sum3 += sum2;

println("sum #1 [1, 25): {sum0}");
println("sum #2 [1, 50): {sum1}");
println("sum #3 [1, 75): {sum2}");
println("Gauss [1, 100]: {sum3}");
```

### Loops
#### While

```
// PRINTING THE ALPHABET

let alphabet = "abcdefghijklmnopqrstuvwxyz";
let mut i = 0;

while(i < alphabet.len()){
    let letter = alphabet[i];

    println(letter);

    i += 1;
}

// PRINTING EVEN NUMBERS IN INTERVAL [1, 100]

let mut o = 1;

while(o <= 100){
    if(o mod 2 != 0){
        o += 1;

        continue;
    }

    println(o);

    o += 1;
}
```

#### For

```
// PRINTING THE ALPHABET

let alphabet = "abcdefghijklmnopqrstuvwxyz";

// in normal order
for(let i = 0; i < alphabet.len(); i += 1){
    let letter = alphabet[i];

    println(letter);
}

// in reverse order
for(let i = alphabet.len() - 1; i >= 0; i -= 1){
    let letter = alphabet[i];

    println(letter);
}

// PRINTING EVEN NUMBERS IN INTERVAL [1, 100]

for(let num = 1; num <= 100; num += 1){
    if(num mod 2 == 0){
        println(num);
    }
}

// PRINTING THE FIRST 10 EVEN NUMBERS IN INTERVAL [1, 100]

let mut even_count = 0;

for(let num = 1; num <= 100; num += 1){
    if(num mod 2 == 0){
        even_count += 1;

        println(num);
    }

    if(even_count == 10){
        stop;
    }
}
```

### Procedures

Procedures or functions look like this:

```
proc fib(value){
    if(value < 2): ret value;

    ret fib(value - 1) + fib(value - 2);
}
```

Parameters, by default, are immutable:

```
proc decrement(from){
    for(;from >= 0; from -= 1){ // trying to increment 'from' will cause a error before runtime
        println(from);
    }
}
```

to make them mutable you use the 'mut' keyword before the parameter name:

```
proc decrement(mut from){
    for(;from >= 0; from -= 1){ // This is fine!
        println(from);
    }
}
```

You can avoid to write the parameters enclosing parenthesis if the procedure has not parameters:

```
proc foo{
    println("Hi!");
}
```

#### Anonymous

Procedures with not name are written like this:

```
let some_proc = anon(value){ret value;};
```

They have the same properties as normal procedures (parameters immutable by default and a short version):

```
let some_proc = anon(mut from){
    let values = array[from];

    for(let i = 0; from >= 1; i += 1, from -= 1){
        values[i] = from;
    }

    ret values;
};
```

```
let some_proc = anon{println("I'm useless, like this programming language");};
```