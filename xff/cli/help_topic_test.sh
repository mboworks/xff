#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Binary-level test of `--help=TOPIC` topic help (the flag-only mechanism) and the
# guiding error when a user reaches for a `help` subcommand out of habit. Anchors on
# stable substrings, not exact wording.

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

# `test_tmpdir` allocates each tree under bashtest's managed scratch root. Its random suffix keeps
# test names out of printed paths, where a name could accidentally satisfy a negative assertion.

_xff_bin() {
  local bin="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/testing/xff"
  if [[ ! -x "${bin}" ]]; then
    bin="$(find "${TEST_SRCDIR}" -type f -name xff -path '*xff/cli/testing/xff' 2>/dev/null | head -1)"
  fi
  echo "${bin}"
}

test::help_topic_flag_prints_entry() {
  local out rc
  out="$("$(_xff_bin)" --help=-regex 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_matches '\-regex' "${out}"
  expect_output_contains 'regular expression' "${out}" # the summary
  expect_output_contains 'test' "${out}"               # kind tag
  expect_output_contains 'whole' "${out}"              # the per-primary details (whole-path anchoring)
}

test::help_full_shows_per_primary_details() {
  # --help=full renders each primary's long description (registry Descriptor.details), not just the
  # one-line summaries; --help=expressions (summaries only) does not.
  local full expr
  full="$("$(_xff_bin)" --help=full 2>&1)"
  expr="$("$(_xff_bin)" --help=expressions 2>&1)"
  expect_output_contains 'batches as many paths' "${full}" # from -exec details
  # shellcheck disable=SC2016  # the backticks are the help's inline-code markup, not a subshell
  expect_output_contains 'needs `--allow-exec`' "${full}"                  # -exec sensitivity note in the details
  expect_output_contains 'reaches back a full relative duration' "${full}" # from -mtime details
  expect_output_not_contains 'batches as many paths' "${expr}"
  expect_output_not_contains 'reaches back a full relative duration' "${expr}"
}

test::help_time_primary_shows_details() {
  # A per-primary topic (`--help=mtime`) resolves the -mtime descriptor and shows its long
  # description, including the xff-only compound-span form and the find-compat note.
  local out
  out="$("$(_xff_bin)" --help=mtime --width=none 2>&1)"
  expect_output_contains 'reaches back a full relative duration' "${out}"
  # shellcheck disable=SC2016  # the backticks are the help's inline-code markup, not a subshell
  expect_output_contains 'rejected by `--config=find`' "${out}"
}

test::help_matching_primaries_show_details() {
  # -path documents that its glob crosses `/` (unlike the shell); -rxc that its content regex is
  # unanchored, distinguishing it from -regex's whole-path anchoring.
  local path rxc
  path="$("$(_xff_bin)" --help=path 2>&1)"
  expect_output_contains 'DO match' "${path}" # `*`/`?` cross `/`
  expect_output_contains '-path ARG, -p ARG' "${path}"
  path="$("$(_xff_bin)" --help=p 2>&1)"
  expect_output_contains '-path ARG, -p ARG' "${path}" # alias topic resolves to the canonical entry
  rxc="$("$(_xff_bin)" --help=rxc 2>&1)"
  expect_output_contains 'unanchored' "${rxc}"
}

test::help_attribute_primaries_show_details() {
  # -perm documents the exact / all-of / any-of prefix grammar; -xtype the follow-the-target rule.
  local perm xtype
  perm="$("$(_xff_bin)" --help=perm 2>&1)"
  expect_output_contains 'ALL the listed bits' "${perm}" # the -MODE all-of rule
  xtype="$("$(_xff_bin)" --help=xtype 2>&1)"
  expect_output_contains "link's TARGET" "${xtype}"
}

test::help_action_primaries_show_details() {
  # -print documents the implicit-default rule; -grep the path:lineno:text form; -fprint the
  # opened-once file handling that anchors the -f* family.
  local print grep fprint
  print="$("$(_xff_bin)" --help=print 2>&1)"
  expect_output_contains 'DEFAULT action' "${print}"
  grep="$("$(_xff_bin)" --help=grep 2>&1)"
  # shellcheck disable=SC2016  # the backticks are the help's inline-code markup, not a subshell
  expect_output_contains 'line-output companion of `-rxc`' "${grep}"
  fprint="$("$(_xff_bin)" --help=fprint 2>&1)"
  expect_output_contains 'opened once' "${fprint}"
}

test::help_reference_time_primaries_show_details() {
  # -newer documents the -newerXY matrix convention; -newermt the time-string (t) form.
  local newer newermt
  newer="$("$(_xff_bin)" --help=newer --width=none 2>&1)"
  expect_output_contains 'where each of X and Y is a=access' "${newer}"
  newermt="$("$(_xff_bin)" --help=newermt 2>&1)"
  expect_output_contains 'a timestamp xff parses' "${newermt}"
}

test::help_traversal_owner_operator_primaries_show_details() {
  # -maxdepth documents the global-positional rule; -user the name/numeric resolution; -a the
  # operator precedence order.
  local maxdepth user op
  maxdepth="$("$(_xff_bin)" --help=maxdepth 2>&1)"
  expect_output_contains 'global positional option' "${maxdepth}"
  user="$("$(_xff_bin)" --help=user 2>&1)"
  expect_output_contains 'passwd database' "${user}"
  op="$("$(_xff_bin)" --help=-a 2>&1)"
  expect_output_contains 'tightest to loosest' "${op}"
}

test::help_topic_flag_resolves_without_dash() {
  expect_matches '\-regex' "$("$(_xff_bin)" --help=regex 2>&1)"
}

test::help_config_explains_tiers_and_style_selection() {
  # `--help=config` explains the layering, style selection (--config / argv[0]), and arming, and
  # pulls the config flags from the SOT. Focused --config help expands this sole related topic.
  local config flag
  config="$("$(_xff_bin)" --help=config 2>&1)"
  expect_output_contains 'lowest to highest precedence' "${config}" # the tier ordering
  expect_output_contains 'argv[0]' "${config}"                      # style selection by invocation name
  expect_output_contains 'NON-ARMING' "${config}"                   # the --xffrc arming rule
  expect_matches '\-\-allow-exec' "${config}"                       # a config flag pulled from the SOT
  # --help=--config leads with the flag and then expands its sole related topic.
  flag="$("$(_xff_bin)" --help=--config 2>&1)"
  expect_output_contains 'lowest to highest precedence' "${flag}"
}

test::help_cookbook_lists_worked_examples() {
  # `--help=cookbook` (aliases examples / recipes) is the task-oriented recipe list; each recipe
  # carries a runnable command. It also folds into --help=full.
  local cookbook full
  cookbook="$("$(_xff_bin)" --help=cookbook 2>&1)"
  expect_output_contains 'Worked examples' "${cookbook}"
  expect_output_contains 'git blame' "${cookbook}"                           # the flagship -exec recipe
  expect_output_contains 'xff . -type f --summary=ext' "${cookbook}"         # a runnable command (global at the tail)
  expect_output_contains 'Ten largest files' "${cookbook}"                   # a recipe task heading
  expect_output_contains 'git blame' "$("$(_xff_bin)" --help=examples 2>&1)" # alias resolves
  full="$("$(_xff_bin)" --help=full 2>&1)"
  expect_output_contains 'Ten largest files' "${full}" # the examples fold into the full reference
}

test::help_list_topic_topics_show_the_topic_list_in_any_case() {
  # `--help=list` used to alias the usage page, which its own table row ("index of every option and
  # expression primary") did not describe - it read as "does not work". It, `topic` and `topics`, in
  # any case, now return the topic list and NOTHING else, byte-identical across all spellings.
  local ref out spelling
  ref="$("$(_xff_bin)" --help=list 2>&1)"
  expect_output_contains 'HELP TOPICS' "${ref}"
  expect_output_contains 'cookbook' "${ref}"     # a topic row
  expect_output_not_contains '-execdir' "${ref}" # not the primaries index (that is --help=all)
  for spelling in topic topics LIST TOPIC TOPICS List Topics; do
    out="$("$(_xff_bin)" --help=${spelling} 2>&1)"
    expect_eq "${ref}" "${out}"
  done
}

test::help_all_shows_grouped_index() {
  # The grouped every-option-and-primary index is --help=all (list is the topic list above).
  local out
  out="$("$(_xff_bin)" --help=all 2>&1)"
  expect_output_contains 'Tests:' "${out}"
  expect_output_contains 'Actions:' "${out}"
  expect_output_contains 'Operators:' "${out}"
}

test::help_expressions_lists_the_annotated_vocabulary() {
  # `--help=expressions` is the grouped Tests/Actions/Operators list with summaries,
  # the full list the usage overview points at.
  local out
  out="$("$(_xff_bin)" --help=expressions 2>&1)"
  expect_output_contains 'Tests:' "${out}"
  expect_output_contains 'Actions:' "${out}"
  expect_matches '\-content' "${out}" # an expression primary is listed
}

test::help_fields_lists_the_placeholder_vocabulary() {
  # `--help=fields` prints the {field} vocabulary: named fields grouped by heading,
  # aliases folded in, plus the dynamic namespaces and qualifiers.
  local out rc
  out="$("$(_xff_bin)" --help=fields 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_output_contains 'Path & name:' "${out}"    # a group heading
  expect_output_contains '{relpath}' "${out}"       # a named field
  expect_matches '\{name\} \{file\}' "${out}"       # an alias folded onto its canonical
  expect_output_contains '{env.NAME}' "${out}"      # a dynamic namespace
  expect_output_contains '{name:s/RE/R/f}' "${out}" # the rewrite qualifier
  expect_output_contains 'stem' "${out}"            # a path-component keyword (read from the SOT)
  # The m// pipeline span diagram (ASCII ranges under each stage).
  expect_output_contains '|________| |________| |_______| |________|' "${out}"
  expect_output_contains 'extract    map each   reduce    rewrite' "${out}"
  # `--help=format` is NOT this topic: it resolves to the --format output-format flag.
  out="$("$(_xff_bin)" --help=format 2>&1)"
  expect_output_contains 'output format' "${out}"
  expect_output_not_contains '{relpath}' "${out}"
}

test::help_stats_documents_the_reductions() {
  # `--help=stats` documents --summary and --histogram. Their flags (and the bucket/measure
  # grammar, carried in --histogram's details) are pulled from the SOT via the "stats" topic tag.
  local out rc
  out="$("$(_xff_bin)" --help=stats 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_matches '\-\-summary' "${out}"
  expect_matches '\-\-histogram' "${out}"
  expect_output_contains 'sum(lines)' "${out}"          # the aggregate grammar (from --histogram details)
  expect_output_contains 'needs an aggregator' "${out}" # the no-bare-metric rule
}

test::help_ignore_explains_the_independent_vcs_axes() {
  local out rc
  out="$("$(_xff_bin)" --help=ignore 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_output_contains "Independent axes" "${out}"
  expect_output_contains "Pattern precedence" "${out}"
  expect_output_contains "--gitignore" "${out}"
  expect_output_contains "--ignore-vcs" "${out}"
  expect_output_contains "--skip-vcs" "${out}"
  expect_output_contains "--no-skip-vcs" "${out}"
  expect_output_contains "implicitly prunes" "${out}"
  expect_output_contains "does not cancel command-line" "${out}"
  out="$("$(_xff_bin)" --help=vcs 2>&1)"
  expect_output_contains "IGNORE AND VCS TRAVERSAL" "${out}"
}

test::help_archive_documents_diving_and_what_is_writable() {
  # `--help=archive` gathers the whole --archive family: the flags come from the SOT via the
  # "archive" topic tag, and the prose carries what the flags alone cannot say - that a member is an
  # ordinary entry, that the container keeps its own identity, and which actions are refused.
  local out rc
  out="$("$(_xff_bin)" --help=archive 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_matches '\-\-archive' "${out}"
  expect_matches '\-\-archive-aggregate' "${out}" # every flag of the family, not just the entry point
  expect_matches '\-\-archive-extract' "${out}"
  expect_matches '\-\-archive-delete' "${out}"
  expect_output_contains "READ-ONLY" "${out}" # the rule the two write flags are exceptions to
  expect_output_contains "a.tar!dir/two.txt" "${out}"
  out="$("$(_xff_bin)" --help=archives 2>&1)" # the plural alias resolves to the same topic
  expect_output_contains "READ-ONLY" "${out}"
}

test::a_value_table_heading_names_the_placeholder_or_nothing() {
  # A flag whose synopsis collapses its values to a <PLACEHOLDER> heads the table with that name; one
  # that spells them inline has no name to use and gets the bare "One of:" - it used to read as the
  # nonsense "One of is one of:".
  local out
  out="$("$(_xff_bin)" --help=--summary 2>&1)"
  expect_output_contains "GROUP is one of:" "${out}"
  out="$("$(_xff_bin)" --help=--archive 2>&1)"
  expect_output_contains "One of:" "${out}"
  expect_output_not_contains "One of is one of:" "${out}"
}

test::help_printf_lists_the_directive_vocabulary() {
  # `--help=printf` prints the % directive table (from engine::PrintfDocs) plus the
  # %{field} escape; --help=full folds the same table in so the full reference is exhaustive.
  local out rc
  out="$("$(_xff_bin)" --help=printf 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_output_contains 'PRINTF DIRECTIVES' "${out}"
  expect_output_contains '%p' "${out}"                # a find % directive
  expect_output_contains '%{NAME}' "${out}"           # the xff field escape
  expect_output_contains 'see --help=fields' "${out}" # qualifier cross-reference
  expect_output_contains 'PRINTF DIRECTIVES' "$("$(_xff_bin)" --help=full 2>&1)"
}

test::help_time_and_size_list_their_vocabularies() {
  # `--help=time` prints the time-format presets (from datetime::FormatDocs); `--help=size`
  # the -size units (from engine::SizeUnitDocs). Both fold into --help=full.
  local out
  out="$("$(_xff_bin)" --help=time 2>&1)"
  expect_output_contains 'TIME FORMATS' "${out}"
  expect_output_contains 'iso8601' "${out}"
  expect_output_contains 'epoch' "${out}"
  out="$("$(_xff_bin)" --help=size --width=none 2>&1)"
  expect_output_contains 'SIZE UNITS' "${out}"
  expect_output_contains 'SI units' "${out}"
  expect_output_contains 'IEC units' "${out}"
  expect_output_contains 'kB / MB' "${out}"
  expect_output_contains 'KiB / MiB' "${out}"
  expect_output_contains '18446744073709551615 B' "${out}"
  expect_output_contains '18EB' "${out}"
  expect_output_contains '15EiB' "${out}"
  expect_output_contains 'ZB' "${out}"
  expect_output_contains 'one zettabyte/zebibyte already exceeds' "${out}"
  # --help=full is exhaustive: it folds in the field, printf, time, and size vocabularies.
  out="$("$(_xff_bin)" --help=full 2>&1)"
  expect_output_contains 'TIME FORMATS' "${out}"
  expect_output_contains 'SIZE UNITS' "${out}"
  expect_output_contains 'PRINTF DIRECTIVES' "${out}"
}

test::help_notice_and_license_reproduce_the_texts() {
  # For single-file binary releases the program must REPRODUCE its notices, not point at files.
  # `--help=notice` (alias notices) renders the third-party manifest also published in NOTICE.md + the build
  # extras this binary has; `--help=license` (alias licenses) embeds the verbatim LICENSE (Apache-2.0).
  local notice license
  notice="$("$(_xff_bin)" --help=notice 2>&1)"
  expect_output_contains 'none (lean build)' "${notice}" # the build-dependent extras line (lean here)
  expect_output_contains 'RE2' "${notice}"               # a core component (from the reproduced NOTICE.md)
  expect_output_contains 'BSD-3-Clause' "${notice}"      # a component's SPDX id in the manifest
  license="$("$(_xff_bin)" --help=license 2>&1)"
  # A complete licensing statement leads with the copyright + grant (Apache's APPENDIX), THEN the
  # verbatim license body - not the bare boilerplate with no owner.
  expect_output_contains 'Copyright M. Boerger, the MBO Works authors' "${license}"
  expect_matches '^xff - eXtended File Find' "${license}" # copyright block heads the output
  expect_output_contains 'Apache License' "${license}"    # the reproduced LICENSE text, in full
  expect_output_contains 'Version 2.0' "${license}"       # ditto (not just a pointer to a file)
  # The plural aliases resolve to the same topics.
  expect_output_contains 'RE2' "$("$(_xff_bin)" --help=notices 2>&1)"
  expect_output_contains 'Apache License' "$("$(_xff_bin)" --help=licenses 2>&1)"

  # The manifest and the generic topic footer are both flowing help text. Neither may bypass the
  # explicit width merely because the canonical NOTICE.md file itself has stable physical lines.
  notice="$("$(_xff_bin)" --width=50 --help=notice 2>&1)"
  while IFS= read -r line; do
    ((${#line} <= 50)) || fail "--help=notice exceeded --width=50: ${line}"
  done <<<"${notice}"
}

test::help_license_component_shows_that_components_license() {
  # `--help=license=COMPONENT` answers "what does the license of this bundled thing SAY" - which
  # neither --help=notice (names only) nor --help=license (xff's own) could answer.
  local out
  out="$("$(_xff_bin)" '--help=license=mboworks/mbo' 2>&1)"
  expect_matches '^xff - eXtended File Find' "${out}" # #142: the grant still leads
  expect_output_contains 'mboworks/mbo' "${out}"
  expect_output_contains 'Apache License' "${out}" # the body of the license it names
  # Core's non-Apache license is embedded too.
  out="$("$(_xff_bin)" --help=license=RE2 2>&1)"
  expect_output_contains 'BSD-3-Clause' "${out}"
  expect_output_contains 'Redistribution and use' "${out}"
  # Component names are proper nouns; the lookup must not be a spelling test.
  expect_output_contains 'BSD-3-Clause' "$("$(_xff_bin)" --help=license=re2 2>&1)"

}

test::help_unknown_license_component_names_the_known_ones() {
  # The topic exists and the COMPONENT does not, so the guiding error lists components (not topics).
  local out rc
  out="$("$(_xff_bin)" --help=license=nope 2>&1)" && rc=0 || rc="${?}"
  expect_eq "2" "${rc}"
  expect_output_contains "no licensed component 'nope'" "${out}"
  expect_output_contains 'RE2' "${out}" # what this binary DOES have
}

test::help_unknown_topic_exits_two() {
  local out rc
  out="$("$(_xff_bin)" --help=-nonesuch 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains 'no help topic' "${out}"
  expect_output_contains "--help=topics" "${out}"
}

test::bare_help_operand_is_guided_not_a_subcommand() {
  # A user typing `xff help` out of git habit gets a guiding error (not a silent
  # attempt to search a path named "help").
  local out rc
  out="$("$(_xff_bin)" help 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains 'not a subcommand' "${out}"
  expect_matches '\-\-help' "${out}"
}

test::bad_flags_are_hard_errors_even_with_help() {
  # A broken command line must never vanish behind pages of help: the meta flags are rendered only
  # after the REST of the arguments parse and validate, so the one-line error keeps the typo visible.
  local bin out
  bin="$(_xff_bin)"
  out="$("${bin}" --help=archive --//xff:xff_full 2>&1)" && fail "unknown flag with help must exit non-zero"
  expect_output_contains "unknown option '--//xff:xff_full'" "${out}"
  expect_output_not_contains 'Archives' "${out}"
  out="$("${bin}" --help --color=bogus 2>&1)" && fail "bad value with help must exit non-zero"
  expect_output_contains "unknown value 'bogus'" "${out}"
  # And a CLEAN help invocation still renders, with the compat single-dash forms intact.
  "${bin}" --help >/dev/null || fail "--help must succeed"
  "${bin}" -help >/dev/null || fail "-help must succeed"
  "${bin}" --help=archive >/dev/null || fail "--help=archive must succeed"
}

test::meta_spelling_inside_exec_stays_a_child_argument() {
  # Meta recognition follows parser boundaries. A child command is an opaque
  # argument run, so its `--help` must not turn xff itself into the usage page.
  local out
  out="$("$(_xff_bin)" --allow-exec . -maxdepth 0 -exec printf 'child:%s\n' --help ';' 2>&1)"
  expect_output_contains 'child:--help' "${out}"
  expect_output_not_contains 'eXtended File Find' "${out}"
}

test::bare_help_operand_passes_through_in_find_mode() {
  # Invoked as `find`, `help` must stay a path operand (find compatibility), so the
  # xff guiding error must NOT fire.
  local tmp out
  tmp="$(test_tmpdir tree)"
  mkdir -p "${tmp}"
  cp "$(_xff_bin)" "${tmp}/find"
  out="$("${tmp}/find" help 2>&1)" || true
  expect_output_not_contains 'not a subcommand' "${out}"
}

test::single_help_pages_end_with_the_help_pointer() {
  # A user who runs `--help=-regex` has never seen the help system's map: the usage page lists the
  # topics, but the entry page says nothing about the index or the help topic. One trailer, on the
  # pages that lack that context only.
  local bin tip
  bin="$(_xff_bin)"
  tip="xff --help=help"
  expect_output_contains "${tip}" "$("${bin}" --help=-regex 2>&1)"  # a primary entry
  expect_output_contains "${tip}" "$("${bin}" --help=--sort 2>&1)"  # a flag entry
  expect_output_contains "${tip}" "$("${bin}" --help=fields 2>&1)"  # a vocabulary topic
  expect_output_contains "${tip}" "$("${bin}" --help=archive 2>&1)" # a CLI-rendered topic
}

test::the_maps_and_the_documents_carry_no_pointer() {
  # The pages that ARE the map do not need pointing at themselves, and a document that gets
  # installed or published must not carry a terminal tip.
  local bin tip page
  bin="$(_xff_bin)"
  tip="xff --help=help"
  for page in --help --help=help --help=list --help=all --help=full --help=expressions \
    --help=notice --help=notices --help=license --help=licenses --help=license=Apache-2.0; do
    expect_output_not_contains "${tip}" "$("${bin}" "${page}" 2>&1)"
  done
  for page in markdown html; do
    expect_output_not_contains "${tip}" "$("${bin}" --help=full "--help-format=${page}" 2>&1)"
  done
}

test::help_rejects_config_only_policy() {
  local out
  out="$("$(_xff_bin)" --help=safety --block-policy-categories=archive 2>&1)" && fail "config-only policy with help must fail"
  expect_output_contains 'is a config-only directive' "${out}"
}

test::comparison_help_includes_summary_context() {
  local topic out
  for topic in compare --compare compare-select --compare-select; do
    out="$("$(_xff_bin)" "--help=${topic}" --width=0)"
    expect_output_contains '--compare=summary' "${out}"
    expect_output_contains '--summary=compare' "${out}"
    expect_output_contains 'Status output' "${out}"
    expect_output_contains 'See also' "${out}"
    expect_output_contains '--summary-precision' "${out}"
    expect_output_contains 'xff --compare=summary left-tree right-tree' "${out}"
    expect_output_contains 'Percentages use all results or all combined' "${out}"
  done
}

test::grammar_selector_help_links_to_regex_topic() {
  local topic out
  for topic in regextype -regextype --regextype re2 pcre; do
    out="$("$(_xff_bin)" "--help=${topic}" --width=0)"
    expect_output_contains 'See also: --help=regex, --help=grammars' "${out}"
    expect_output_contains 'grammars' "${out}"
    case "${topic}" in
      regextype | --regextype)
        expect_output_contains 'REGEX GRAMMARS' "${out}"
        expect_output_contains 'canonical external references' "${out}"
        ;;
      *) expect_output_not_contains 'canonical external references' "${out}" ;;
    esac
  done
}

test::regex_help_covers_matching_and_controls() {
  local out root
  out="$("$(_xff_bin)" --help=regex --width=0)"
  expect_output_contains 'REGEX MATCHING' "${out}"
  expect_output_contains 'REGEX MATCHING' "$("$(_xff_bin)" --help=reg --width=0)"
  expect_output_contains '-regex' "${out}"
  expect_output_contains '-grep' "${out}"
  expect_output_contains '--case' "${out}"
  expect_output_contains '--regextype' "${out}"
  expect_output_contains '--context' "${out}"
  expect_output_contains 'canonical external references' "${out}"
  out="$("$(_xff_bin)" --help=regextype --width=0)"
  expect_output_contains 'GRAMMAR is one of:' "${out}"
  expect_output_contains '--regextype=<GRAMMAR>' "${out}"
  root="$(test_tmpdir regex_examples)"
  printf 'TODO fixme\n' >"${root}/example.cc"
  out="$("$(_xff_bin)" "${root}" --regextype=RE2 -regex '.*[.](cc|h)')"
  expect_output_contains 'example.cc' "${out}"
  out="$("$(_xff_bin)" "${root}" --regextype=RE2 --case=insensitive -grep 'todo|fixme')"
  expect_output_contains 'TODO fixme' "${out}"
}

test::related_help_is_available_across_topics() {
  local topic out
  for topic in fields printf time size content output compare ignore config safety archive stats \
    environment styles extras notice license help list all expressions cookbook; do
    out="$("$(_xff_bin)" "--help=${topic}" --width=0)"
    expect_output_contains 'See also: --help=' "${out}"
  done
}

test::focused_help_links_to_topics_and_related_controls() {
  local out
  out="$("$(_xff_bin)" --help=--diff-context --width=0)"
  expect_output_contains '--help=compare' "${out}"
  out="$("$(_xff_bin)" --help=-grep --width=0)"
  expect_output_contains '--help=content' "${out}"
  expect_output_contains '--help=--context' "${out}"
  out="$("$(_xff_bin)" --help=-size --width=0)"
  expect_output_contains '--help=size' "${out}"
}

test::long_help_is_the_full_reference_without_navigation_expansion() {
  local full long
  full="$("$(_xff_bin)" --help=full --width=0)"
  long="$("$(_xff_bin)" --help=long --width=0)"
  expect_eq "${full}" "${long}"
  expect_output_not_contains 'See also: --help=' "${long}"
}

test::focused_vocabulary_help_appends_the_shared_reference() {
  local out
  out="$("$(_xff_bin)" --help=--template --width=0)"
  expect_output_contains 'FIELDS' "${out}"
  out="$("$(_xff_bin)" --help=-printfln --width=0)"
  expect_output_contains 'PRINTF DIRECTIVES' "${out}"
  out="$("$(_xff_bin)" --help=-println --width=0)"
  expect_output_not_contains 'PRINTF DIRECTIVES' "${out}"
  out="$("$(_xff_bin)" --help=--after-context --width=0)"
  expect_output_not_contains 'FIELDS' "${out}"
  out="$("$(_xff_bin)" --help=--time-format --width=0)"
  expect_output_contains 'TIME FORMATS' "${out}"
}

test::long_reference_pointers_follow_the_output_format() {
  local out
  out="$("$(_xff_bin)" --help=long --help-format=markdown)"
  expect_output_contains '[Regex matching](#topic-regex)' "${out}"
  out="$("$(_xff_bin)" --help=long --help-format=html)"
  expect_output_contains 'href="#topic-regex">Regex matching</a>' "${out}"
  out="$("$(_xff_bin)" --help=long --help-format=roff)"
  expect_output_contains '.B Regex matching' "${out}"
}

test::summary_scope_help_explains_conditional_defaults() {
  local out
  out="$("$(_xff_bin)" --help=summary-scope --width=0)"
  expect_output_contains 'the default is' "${out}"
  # shellcheck disable=SC2016 # Backticks are literal help markup.
  expect_output_contains '`all` outside comparison and `compare` when `--compare` is active' "${out}"
  expect_output_contains 'An explicit scope overrides that conditional default' "${out}"
  expect_output_contains 'regardless of option order' "${out}"
}

test::ini_width_preferences_apply_without_running_configured_actions() {
  local tmp actual expected
  tmp="$(test_tmpdir)"
  printf '%s\n' '--width=auto:100' >"${tmp}/system.ini"
  printf '%s\n' '--width=auto:60' "-exec touch ${tmp}/must-not-exist \\;" >"${tmp}/user.ini"
  actual="$(XFF_TEST_SYSTEM_CONFIG="${tmp}/system.ini" XFF_TEST_USER_CONFIG="${tmp}/user.ini" COLUMNS=200 "$(_xff_bin)" --help=width)"
  expected="$("$(_xff_bin)" --help=width --width=60)"
  expect_eq "${expected}" "${actual}"
  expect_eq "absent" "$(if [[ -e "${tmp}/must-not-exist" ]]; then echo present; else echo absent; fi)"
  actual="$(XFF_TEST_USER_CONFIG="${tmp}/user.ini" "$(_xff_bin)" --help=width --width=90)"
  expected="$("$(_xff_bin)" --help=width --width=90)"
  expect_eq "${expected}" "${actual}"
}

test::ini_width_preferences_apply_to_comparison_summaries() {
  local tmp actual expected
  tmp="$(test_tmpdir)"
  mkdir "${tmp}/left" "${tmp}/right"
  printf x >"${tmp}/left/item.txt"
  printf '%s\n' '--width=auto:60' >"${tmp}/user.ini"
  actual="$(XFF_TEST_USER_CONFIG="${tmp}/user.ini" COLUMNS=200 "$(_xff_bin)" --compare=summary "${tmp}/left" "${tmp}/right" --summary=ext)"
  expected="$("$(_xff_bin)" --compare=summary "${tmp}/left" "${tmp}/right" --summary=ext --width=60)"
  expect_eq "${expected}" "${actual}"
}

test::unknown_help_selectors_suggest_registered_spellings_without_running_actions() {
  local selector expected out rc tmp
  tmp="$(test_tmpdir)"
  for selector in wildth --wildth comapre recpies -naem; do
    case "${selector}" in
      wildth) expected=width ;;
      --wildth) expected=--width ;;
      comapre) expected=compare ;;
      recpies) expected=recipes ;;
      -naem) expected=-name ;;
    esac
    out="$("$(_xff_bin)" "--help=${selector}" "${tmp}" -exec touch "${tmp}/must-not-exist" ';' 2>&1)" && rc=0 || rc=$?
    expect_eq 2 "${rc}"
    expect_output_contains "'--help=${expected}'" "${out}"
  done
  expect_eq absent "$(if [[ -e "${tmp}/must-not-exist" ]]; then echo present; else echo absent; fi)"
}

test_runner
