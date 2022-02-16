// The autocodex wisdom bot. Use an up to date deno and run with:
// $ deno run --allow-net --allow-run=./generator autocodex.ts 

import { Client } from "https://deno.land/x/irc/mod.ts";

const config = {
  channel: "#c++",
  server: "irc.quakenet.org",
  botName: "autocodex",
  minTimeSecs: 8 * 3600,
  maxTimeSecs: 24 * 3600,
};

const client = new Client({
  nick: config.botName,
  channels: [config.channel],
  realname: "Pero Tulkkinen",
  username: "autocodex",
});

// Generate a random millisecond duration based on the config.
function randTime(): number {
  const range = config.maxTimeSecs - config.minTimeSecs;
  return Math.floor(1000 * (config.minTimeSecs + Math.random() * range));
}

// Generate some fantastic insight, with an optional query.
async function generate(query?: string): Promise<string> {
  const command = ["./generator", "-f", "filtered.txt"];
  if (query) {
    command.push("-w");
    command.push(query);
  }

  const p = Deno.run({
    cmd: command,
    stdout: "piped",
    stderr: "piped",
  });

  const timerName = query ? "generateWithQuery" : "generate";
  console.time(timerName);

  let generated = "";
  if (p.stdout && p.stderr) {
    const decoder = new TextDecoder();
    const stdout = decoder.decode(await p.output());
    if (stdout) {
      generated = stdout;
    }

    const stderr = decoder.decode(await p.stderrOutput());
    if (stderr) {
      console.log(`Generator error: ${stderr}`);
    }
  }

  await p.status();
  p.close();

  console.timeEnd(timerName);

  return generated;
}

// Spew some nonsense into the channel that no one asked for.
async function autoSpew() {
  const wisdom = await generate();
  if (wisdom) {
    client.privmsg(config.channel, wisdom);
  }
  setTimeout(autoSpew, randTime());
}

// Called when the bot sees action in a channel.
client.on("privmsg:channel", async ({source, params}) => {
  // Check if someone is seeking our wisdom. Trigger on the configured nick, as
  // well as the current actual nick.
  const text = params.text;
  if (
    text.indexOf(`${client.state.user.nick}:`) == 0 ||
    text.indexOf(`${config.botName}:`) == 0
  ) {
    const wisdom = await generate(text.substr(text.indexOf(":") + 1).trim());
    if (wisdom) {
      // Provide a thoughtful answer to the query.
      client.privmsg(params.target, `${source?.name}: ${wisdom}`);
    }
  }
});

// Nick regain check.
setInterval(() => {
  if (client.state.user.nick != config.botName) {
    console.log(`Attempting to regain ${config.botName}`);
    client.nick(config.botName);
  }
}, 60 * 1000);

// Start the autoSpew loop.
setTimeout(autoSpew, randTime());

await client.connect("irc.quakenet.org", 6667);
