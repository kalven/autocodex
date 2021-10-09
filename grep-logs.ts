// IRC log grepper - converts an IRC log into the simple format expected by the
// generator program. Run with:
// $ deno run --allow-read grep-logs.ts irclog.txt > filtered.txt

import { readLines } from "https://deno.land/std@0.110.0/io/mod.ts";

// Match our person of interest.
const poiMatch = /^codex(_*|\d)$/i;
// Match any nick at the start of a message.
const nickMatch = /^[A-Za-z0-9_^-]+:/;
// Skip any messages that match these.
const filters = [
  /^(<<|{)/, // Geordi invocation
  /https?:/,
];
// Skip any messages with fewer than this many tokens.
const minTokens = 4;

// Returns true if the message should be filtered out.
function filterMessage(message: string): boolean {
  for (const filter of filters) {
    if (message.match(filter)) {
      return true;
    }
  }
  if (message.split(" ").length < minTokens) {
    return true;
  }

  return false;
}

const encoder = new TextEncoder();

const fileReader = await Deno.open(Deno.args[0]);
for await (const line of readLines(fileReader)) {
  // The format we're dealing with (weechat) is in three tab-separated parts:
  // date and time, nick and message. The message part may have embedded tabs,
  // so we may see more than three parts.
  const parts = line.split("\t");
  if (parts.length < 3) {
    continue;
  }

  // Check if it's our person of interest.
  if (!parts[1].match(poiMatch)) {
    continue;
  }

  // The generator does token splitting by space only, and embedded tabs in
  // tokens make little sense, so we'll replace them with space here. In the
  // case where there are no tabs, this is equivalent to parts[2].trim().
  let message = parts.slice(2).join(" ").trim();

  if (filterMessage(message)) {
    continue;
  }

  // If the message is addressed to someone, like "nick: ...", then we trim away
  // the nick part (unless it's "std::...").
  const nm = message.match(nickMatch);
  if (nm && !message.match(/^std::/)) {
    message = message.substr(nm[0].length).trim();
  }

  Deno.stdout.writeSync(encoder.encode(message + "\n"));
}
