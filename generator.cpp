#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include <unistd.h>
#include <strings.h>

// Flags:
// -m Max candidates when generating a sentence.
int flag_max_candidates = 25'000;
// -M Max candidates when generating a sentence with an include word.
int flag_include_max_candidates = 100'000;
// -t Min tokens in a sentence.
int flag_min_tokens = 6;
// -T Max tokens in a sentence.
int flag_max_tokens = 35;
// -w Word to try to include (when non-null).
const char* flag_include_word = nullptr;
// -f Input file.
const char* flag_input_file = nullptr;

// Debug flags:
// -c Number of sentences to output.
int flag_sentence_count = 1;
// -x Exit after building markov table.
bool flag_build_only = false;
// -s Random seed.
const char* flag_random_seed = nullptr;

// Lines with fewer than this many tokens are filtered at input.
const int short_sentence_input_threshold = 4;

std::vector<std::string> split(const std::string& line) {
  std::vector<std::string> result;

  std::size_t offset = 0;
  for (;;) {
    auto token_start = line.find_first_not_of(' ', offset);
    if (token_start == std::string::npos) {
      break;
    }
    auto token_end = line.find_first_of(' ', token_start);
    auto count = token_end == std::string::npos ? std::string::npos
                                                : token_end - token_start;
    result.push_back(line.substr(token_start, count));
    offset = token_end;
  }

  return result;
}

void clean_unmatched_quote(std::string& token, char quotechar) {
  if (token[0] == quotechar) {
    if (token.back() != quotechar) {
      token.erase(token.begin());
    }
  } else if (token.back() == quotechar) {
    token.pop_back();
  }
}

void clean_token(std::string& token) {
  if (token.empty()) {
    return;
  }
  // Keep tokens like '"word"' quoted, but clean up '"word' and 'word"'.
  clean_unmatched_quote(token, '"');
}

bool is_stopword(const char* word) {
  char last = word[std::strlen(word) - 1];
  return last == '.' || last == '?';
}

bool match_sentence(const std::vector<const char*>& sentence,
                    const std::unordered_set<const char*>& match_tokens) {
  for (const char* word : sentence) {
    if (match_tokens.count(word)) {
      return true;
    }
  }
  return false;
}

std::string sentence_to_string(const std::vector<const char*>& sentence) {
  std::string result;
  for (std::size_t j = 0; j != sentence.size(); ++j) {
    const char* word = sentence[j];
    if (j == sentence.size() - 1) {
      if (is_stopword(word)) {
        auto len = std::strlen(word);
        if (word[len - 1] == '.') {
          result.append(word, len - 1);
        } else {
          result.append(word, len);
        }
      } else {
        result += word;
      }
    } else {
      result += word;
      result += ' ';
    }
  }
  return result;
}

using trigram = std::array<const char*, 3>;

// Predicates for ordering trigrams, the first orders only on the first
// word. The second orders on the first and second word.
constexpr auto trigram_0_comp = [](const trigram& a, const trigram& b) {
  return a[0] < b[0];
};
constexpr auto trigram_01_comp = [](const trigram& a, const trigram& b) {
  if (a[0] != b[0]) {
    return a[0] < b[0];
  }
  return a[1] < b[1];
};

struct markov_table {
  std::vector<trigram> trigrams;
  std::size_t starts;
};

std::size_t uniform_int(std::mt19937& gen, std::size_t count) {
  std::uniform_int_distribution<std::size_t> dist(0, count - 1);
  return dist(gen);
}

std::vector<const char*> generate_sentence_impl(std::mt19937& gen,
                                                const markov_table& table) {
  std::vector<const char*> sentence;

  // Pick a random starting point. This gives us the first two tokens.
  trigram current = table.trigrams[uniform_int(gen, table.starts)];
  sentence.push_back(current[1]);
  sentence.push_back(current[2]);

  auto shift = [](trigram& a) {
    a[0] = a[1];
    a[1] = a[2];
    a[2] = nullptr;
  };

  // Repeatedly find the next token until we're done.
  for (int i = 2; i != flag_max_tokens; ++i) {
    shift(current);

    // This gives us the range of all trigrams where the first two tokens match
    // the last two generated. If this range is empty, we quit since we have
    // nowhere to go.
    auto [it, it2] = std::equal_range(
        table.trigrams.begin(), table.trigrams.end(), current, trigram_01_comp);

    std::size_t count = it2 - it;
    if (count == 0) {
      break;
    }

    current = it[uniform_int(gen, count)];
    sentence.push_back(current.back());

    if (is_stopword(current.back())) {
      break;
    }
  }
  return sentence;
}

std::string generate_sentence(
    std::mt19937& gen, const markov_table& table,
    const std::unordered_set<const char*>& match_tokens) {
  // If an include word has been specified, then we'll first try generating
  // sentences that include it.
  if (!match_tokens.empty()) {
    for (int i = 0; i != flag_include_max_candidates; ++i) {
      auto sentence = generate_sentence_impl(gen, table);
      if (sentence.size() < flag_min_tokens) {
        continue;
      }
      if (!match_sentence(sentence, match_tokens)) {
        continue;
      }
      return sentence_to_string(sentence);
    }
  }

  // If there's no include word (or if searching for it failed), then we'll
  // continue to try to generate sentences without it.
  for (int i = 0; i != flag_max_candidates; ++i) {
    auto sentence = generate_sentence_impl(gen, table);
    if (sentence.size() < flag_min_tokens) {
      continue;
    }
    return sentence_to_string(sentence);
  }

  // With appropriate input and candidate flags, we shouldn't get here.
  return "";
}

int flag_int(const char* value) {
  char* eptr;
  errno = 0;
  long val = strtol(value, &eptr, 10);
  if (errno != 0 || *eptr != 0) {
    std::cerr << "invalid flag value: '" << value << "'\n";
    std::exit(1);
  }
  return val;
}

int main(int argc, char** argv) {
  // Parse flags.
  int c;
  while ((c = getopt(argc, argv, "c:m:M:t:T:w:f:s:x")) != -1) {
    switch (c) {
      case 'c':
        flag_sentence_count = flag_int(optarg);
        break;
      case 'm':
        flag_max_candidates = flag_int(optarg);
        break;
      case 'M':
        flag_include_max_candidates = flag_int(optarg);
        break;
      case 't':
        flag_min_tokens = flag_int(optarg);
        break;
      case 'T':
        flag_max_tokens = std::max(2, flag_int(optarg));
        break;
      case 'w':
        flag_include_word = optarg;
        break;
      case 'f':
        flag_input_file = optarg;
        break;
      case 'x':
        flag_build_only = true;
        break;
      case 's':
        flag_random_seed = optarg;
        break;
      default:
        std::exit(1);
        break;
    }
  }

  if (!flag_input_file) {
    std::cerr << "Must specify input file with -f\n";
    std::exit(1);
  }
  std::ifstream in(flag_input_file);

  // Set up random generator.
  std::random_device rd;
  std::mt19937 gen(rd());

  if (flag_random_seed) {
    // Re-seed with a specific seed, if needed.
    auto seed_len = std::strlen(flag_random_seed);
    std::seed_seq seed(flag_random_seed, flag_random_seed + seed_len);
    gen = std::mt19937(seed);
  }

  // The markov table contains all trigrams that have been seen in sequence in
  // the input text. Consider the following input line:
  //
  //   "This is an example line"
  //
  // Which will result in the following entries in the table:
  //
  //   [0, "This", "is"]
  //   ["This", "is", "an"]
  //   ["is", "an", "example"]
  //   ["an", "example", "line."]
  //
  // A few things to note:
  //
  // 1) The trigram with a 0 in the first position is called a "start"
  // trigram. Sentence generation begins by picking a start trigram at
  // random. This gives us the first two tokens of the sentence.
  //
  // 2) Generation proceeds by repeatedly picking a trigram at random whose
  // first two tokens match the last two generated tokens, respectively.
  //
  // 3) The last token of the final trigram in an input line has a period
  // added. This marks it as a "stop" token. Sentence generation is done until
  // we've reached a max token count, or until we find a stop token.
  //
  // 4) The same trigram may occur multiple times in the table. This is
  // intentional and used to do weighted random picking. For example, if these
  // three trigrams are present: [A,B,C], [A,B,C], [A,B,D], then given A,B we
  // are twice as likely to generate a C than a D.
  //
  // 5) As an optimization, tokens are interned and the table just stores
  // pointers to strings. The contents of the tokens are only used when the
  // sentence is finally turned into a string.
  //
  // 6) As a second optimization, the table is sorted. This means we can do a
  // binary search to find the next token.

  markov_table table;

  // This is used to intern all tokens seen in the input. All other structures
  // that deal with tokens (as char pointer) point to these strings.
  std::unordered_set<std::string> all_tokens;

  std::string line;
  while (std::getline(in, line)) {
    auto tokens = split(line);

    // Skip short sentences. The input *must* have at least three tokens.
    static_assert(short_sentence_input_threshold >= 3);
    if (tokens.size() < short_sentence_input_threshold) {
      continue;
    }

    std::vector<const char*> tokens_cstr;

    // First loop over tokens and clean them up.
    for (std::size_t i = 0; i != tokens.size(); ++i) {
      clean_token(tokens[i]);
      if (i == tokens.size() - 1) {
        // Make the last token a terminating one.
        if (!is_stopword(tokens[i].c_str())) {
          tokens[i].push_back('.');
        }
      }
      // Intern the token.
      const char* ptr = all_tokens.insert(std::move(tokens[i])).first->c_str();
      tokens_cstr.push_back(ptr);
    }

    // Create trigrams from the input and insert into the table.
    for (std::size_t i = 0; i != tokens_cstr.size() - 2; ++i) {
      if (i == 0) {
        // Start tokens.
        table.trigrams.push_back(
            trigram{nullptr, tokens_cstr[0], tokens_cstr[1]});
      }
      table.trigrams.push_back(
          trigram{tokens_cstr[i], tokens_cstr[i + 1], tokens_cstr[i + 2]});
    }
  }

  // Sort the table - we don't care about the order of the last part.
  std::sort(table.trigrams.begin(), table.trigrams.end(), trigram_01_comp);

  // Calculate the count of start entries.
  auto starts_end = std::upper_bound(
      table.trigrams.begin(), table.trigrams.end(), trigram{}, trigram_0_comp);
  table.starts = starts_end - table.trigrams.begin();

  if (table.starts == 0) {
    std::cerr << "no input\n";
    return 1;
  }

  std::unordered_set<const char*> match_tokens;
  if (flag_include_word) {
    for (const std::string& token : all_tokens) {
      if (strcasestr(token.c_str(), flag_include_word)) {
        match_tokens.insert(token.c_str());
      }
    }
  }

  if (flag_build_only) {
    std::cerr << "token count: " << all_tokens.size() << "\n"
              << "table size: " << table.trigrams.size() << "\n"
              << "starts: " << table.starts << "\n"
              << "match tokens: " << match_tokens.size() << "\n";
    return 0;
  }

  // We're ready to generate some sentences.
  for (int c = 0; c != flag_sentence_count; ++c) {
    std::string sentence = generate_sentence(gen, table, match_tokens);
    if (!sentence.empty()) {
      std::cout << sentence << "\n";
    }
  }
}
