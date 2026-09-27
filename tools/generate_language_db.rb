#!/usr/bin/env ruby
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

# Convert github-linguist's pinned languages.yml to xff's compact, deterministic
# extension vocabulary. Linguist resolves ambiguous suffixes with content
# heuristics that xff intentionally does not run: retain a preferred core winner
# where one exists, and preserve other claims as shared filtering candidates.

require "json"
require "yaml"

CORE_WINNERS = {
  ".asm" => "Assembly",
  ".cs" => "C#",
  ".d" => "D",
  ".ex" => "Elixir",
  ".h" => "C",
  ".hh" => "C++",
  ".html" => "HTML",
  ".json" => "JSON",
  ".m" => "Objective-C",
  ".md" => "Markdown",
  ".ml" => "OCaml",
  ".mm" => "Objective-C++",
  ".php" => "PHP",
  ".pl" => "Perl",
  ".pm" => "Perl",
  ".r" => "R",
  ".rs" => "Rust",
  ".scm" => "Scheme",
  ".sql" => "SQL",
  ".ts" => "TypeScript",
  ".tsx" => "TypeScript",
  ".yaml" => "YAML",
  ".yml" => "YAML",
}.freeze

abort "usage: #{$PROGRAM_NAME} INPUT.yml OUTPUT.json" unless ARGV.length == 2

languages = YAML.safe_load(File.read(ARGV.fetch(0)))
extension_claims = Hash.new { |map, key| map[key] = [] }
filename_claims = Hash.new { |map, key| map[key] = [] }
languages.each do |name, metadata|
  metadata.fetch("extensions", []).each { |extension| extension_claims[extension] << name }
  metadata.fetch("filenames", []).each { |filename| filename_claims[filename] << name }
end

extension_winners = extension_claims.each_with_object({}) do |(extension, claims), winners|
  winner = claims.one? ? claims.first : CORE_WINNERS[extension]
  winners[extension] = winner if winner
end
filename_winners = filename_claims.each_with_object({}) do |(filename, claims), winners|
  winners[filename] = claims.first if claims.one?
end

result = languages.to_h do |name, metadata|
  entry = {"source" => "github-linguist 9.6.0"}
  %w[type color group aliases].each { |field| entry[field] = metadata[field] if metadata.key?(field) }
  extensions = metadata.fetch("extensions", []).select { |extension| extension_winners[extension] == name }
  filenames = metadata.fetch("filenames", []).select { |filename| filename_winners[filename] == name }
  entry["extensions"] = extensions unless extensions.empty?
  entry["filenames"] = filenames unless filenames.empty?
  shared_extensions = metadata.fetch("extensions", []) - extensions
  shared_filenames = metadata.fetch("filenames", []) - filenames
  entry["shared_extensions"] = shared_extensions unless shared_extensions.empty?
  entry["shared_filenames"] = shared_filenames unless shared_filenames.empty?
  [name, entry]
end

File.write(ARGV.fetch(1), JSON.generate(result) << "\n")
