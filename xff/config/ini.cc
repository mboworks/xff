// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "xff/config/ini.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/ascii.h"

namespace xff::config {
namespace {

class IniLexer {
 public:
  explicit IniLexer(std::string_view text) : text_(text) {}

  bool Done() const { return pos_ == text_.size(); }

  IniLine Next() {
    const std::size_t start = pos_;
    IniLine line{.number = number_};
    std::size_t end = text_.size();
    while (!Done()) {
      const char ch = text_[pos_++];
      if (ch == '\n') {
        ++number_;
        if (quote_ == '\0') {
          end = pos_ - 1;
          break;
        }
      }
      if (quote_ == '\0' && (ch == ';' || (ch == '#' && !started_))) {
        end = pos_ - 1;
        SkipComment();
        break;
      }
      ReadCharacter(ch);
    }
    if (quote_ != '\0') {
      error_ = quote_ == '\'' ? "unterminated single quote" : "unterminated double quote";
    }
    Flush();
    line.syntax_error = std::exchange(error_, {});
    line.text = std::string(absl::StripAsciiWhitespace(text_.substr(start, end - start)));
    if (line.syntax_error.empty()) {
      line.tokens = std::move(words_);
    }
    words_.clear();
    quote_ = '\0';
    return line;
  }

 private:
  void SkipComment() {
    while (!Done() && text_[pos_] != '\n') {
      ++pos_;
    }
    if (!Done()) {
      ++pos_;
      ++number_;
    }
  }

  void ReadCharacter(char ch) {
    if (quote_ != '\0') {
      Quoted(ch);
    } else if (absl::ascii_isspace(ch)) {
      Flush();
    } else if (ch == '\'' || ch == '"') {
      quote_ = ch;
      started_ = true;
    } else if (ch == '\\') {
      Escape(false);
    } else {
      Append(ch);
    }
  }

  void Append(char ch) {
    word_.push_back(ch);
    started_ = true;
  }

  void Flush() {
    if (started_) {
      words_.push_back(std::move(word_));
      word_ = {};
      started_ = false;
    }
  }

  void Escape(bool double_quoted) {
    if (Done()) {
      error_ = "trailing backslash";
      return;
    }
    const char next = text_[pos_];
    if (next == '\n') {
      ++pos_;
      ++number_;
      return;
    }
    if (double_quoted && next != '$' && next != '`' && next != '"' && next != '\\') {
      Append('\\');
      return;
    }
    ++pos_;
    Append(next);
  }

  void Quoted(char ch) {
    if (ch == quote_) {
      quote_ = '\0';
    } else if (ch == '\\' && quote_ == '"') {
      Escape(true);
    } else {
      Append(ch);
    }
  }

  std::string_view text_;
  std::size_t pos_ = 0;
  std::size_t number_ = 1;
  char quote_ = '\0';
  bool started_ = false;
  std::string word_;
  std::string error_;
  std::vector<std::string> words_;
};

}  // namespace

ConfigFile ParseIni(std::string_view text) {
  ConfigFile config;
  IniLexer lexer(text);
  while (!lexer.Done()) {
    IniLine line = lexer.Next();
    if (line.text.empty() && line.syntax_error.empty()) {
      continue;
    }
    if (line.syntax_error.empty() && line.text.front() == '[' && line.text.back() == ']') {
      const std::string_view name =
          absl::StripAsciiWhitespace(std::string_view(line.text).substr(1, line.text.size() - 2));
      config.named.push_back({.name = std::string(name), .number = line.number});
    } else if (config.named.empty()) {
      config.globals.insert(config.globals.end(), line.tokens.begin(), line.tokens.end());
      config.global_lines.push_back(std::move(line));
    } else {
      config.named.back().lines.push_back(std::move(line));
    }
  }
  return config;
}

}  // namespace xff::config
