# Autocodex

This is an IRC bot that generates sentences using a 2nd order Markov chain. The
bot is split into two parts. The Markov chain sentence generator is in
`generator.cpp`. The IRC part is in `autocodex.ts` and depends on
[Deno](https://deno.land).

## Running the bot

Compile the generator with a C++17 capable compiler, like GCC:

```bash
$ g++ -o generator -O3 -std=c++17 generator.cpp
```

Run the IRC bits with Deno:

```bash
$ deno run --allow-net --allow-run=./generator autocodex.ts
```

By default, the generator looks for input sentences in `filtered.txt`. This file
needs to be created and should contain one sentence per line. The helper program
`grep-logs.ts` can produce such a file given an IRC log in WeeChat format.

# License

The project is licensed under the MIT license and also availble as public
domain.
