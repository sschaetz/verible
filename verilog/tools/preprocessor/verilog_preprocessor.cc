// Copyright 2017-2022 The Verible Authors.
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

#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/usage.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "common/util/file_util.h"
#include "common/util/init_command_line.h"
#include "common/util/subcommand.h"
#include "verilog/analysis/flow_tree.h"
#include "verilog/analysis/verilog_analyzer.h"
#include "verilog/analysis/verilog_project.h"
#include "verilog/parser/verilog_lexer.h"
#include "verilog/preprocessor/verilog_preprocess.h"
#include "verilog/transform/strip_comments.h"

using verible::SubcommandArgsRange;
using verible::SubcommandEntry;

// TODO(karimtera): Add a boolean flag to configure the macro expansion.
ABSL_FLAG(int, limit_variants, 20, "Maximum number of variants printed");

static absl::Status StripComments(const SubcommandArgsRange& args,
                                  std::istream&, std::ostream& outs,
                                  std::ostream&) {
  // Parse the arguments into a FileList.
  std::vector<absl::string_view> cmdline_args(args.begin(), args.end());
  auto parsed_file_list =
      verilog::ParseSourceFileListFromCommandline(cmdline_args);
  if (!parsed_file_list.ok()) {
    return parsed_file_list.status();
  }
  const auto& files = parsed_file_list->file_paths;

  if (files.empty()) {
    return absl::InvalidArgumentError(
        "Missing file argument.  Use '-' for stdin.");
  }
  const absl::string_view source_file = files[0];
  std::string source_contents;
  if (auto status = verible::file::GetContents(source_file, &source_contents);
      !status.ok()) {
    return status;
  }

  char replace_char;
  if (args.size() == 1) {
    replace_char = ' ';
  } else if (args.size() == 2) {
    absl::string_view replace_str(args[1]);
    if (replace_str.empty())
      replace_char = '\0';
    else if (replace_str.length() == 1)
      replace_char = replace_str[0];
    else
      return absl::InvalidArgumentError(
          "Replacement must be a single character.");
  } else {
    return absl::InvalidArgumentError("Too many arguments.");
  }

  verilog::StripVerilogComments(source_contents, &outs, replace_char);

  return absl::OkStatus();
}

static absl::Status PreprocessSingleFile(
    absl::string_view source_file,
    const std::vector<verilog::TextMacroDefinition>& defines,
    std::ostream& outs, std::ostream& message_stream) {
  std::string source_contents;
  if (auto status = verible::file::GetContents(source_file, &source_contents);
      !status.ok()) {
    message_stream << source_file << status;
    return status;
  }
  verilog::VerilogPreprocess::Config config;
  config.filter_branches = true;
  // config.expand_macros=1;
  verilog::VerilogPreprocess preprocessor(config);

  // Setting defines in the preprocessor.
  for (const auto& define : defines) {
    preprocessor.SetExternalDefine(define.name, define.value);
  }

  verilog::VerilogLexer lexer(source_contents);
  verible::TokenSequence lexed_sequence;
  for (lexer.DoNextToken(); !lexer.GetLastToken().isEOF();
       lexer.DoNextToken()) {
    // For now we will store the syntax tree tokens only, ignoring all the
    // white-space characters. however that should be stored to output the
    // source code just like it was, but with conditionals filtered.
    if (verilog::VerilogLexer::KeepSyntaxTreeTokens(lexer.GetLastToken())) {
      lexed_sequence.push_back(lexer.GetLastToken());
    }
  }
  verible::TokenStreamView lexed_streamview;
  // Initializing the lexed token stream view.
  InitTokenStreamView(lexed_sequence, &lexed_streamview);
  verilog::VerilogPreprocessData preprocessed_data =
      preprocessor.ScanStream(lexed_streamview);
  auto& preprocessed_stream = preprocessed_data.preprocessed_token_stream;
  for (auto u : preprocessed_stream) outs << *u << '\n';
  for (auto& u : preprocessed_data.errors) outs << u.error_message << '\n';

  return absl::OkStatus();
}

static absl::Status MultipleCU(const SubcommandArgsRange& args, std::istream&,
                               std::ostream& outs,
                               std::ostream& message_stream) {
  // Parse the arguments into a FileList.
  std::vector<absl::string_view> cmdline_args(args.begin(), args.end());
  auto parsed_file_list =
      verilog::ParseSourceFileListFromCommandline(cmdline_args);
  if (!parsed_file_list.ok()) {
    return parsed_file_list.status();
  }
  const auto& files = parsed_file_list->file_paths;
  const auto& defines = parsed_file_list->defines;

  if (files.empty()) {
    return absl::InvalidArgumentError("ERROR: Missing file argument.");
  }
  for (const absl::string_view source_file : files) {
    message_stream << source_file << ":\n";
    auto status =
        PreprocessSingleFile(source_file, defines, outs, message_stream);
    if (!status.ok()) return status;
    outs << '\n';
  }
  return absl::OkStatus();
}

static absl::Status GenerateVariants(const SubcommandArgsRange& args,
                                     std::istream&, std::ostream& outs,
                                     std::ostream& message_stream) {
  // Parse the arguments into a FileList.
  std::vector<absl::string_view> cmdline_args(args.begin(), args.end());
  auto parsed_file_list =
      verilog::ParseSourceFileListFromCommandline(cmdline_args);
  if (!parsed_file_list.ok()) {
    return parsed_file_list.status();
  }
  const auto& files = parsed_file_list->file_paths;
  // TODO(karimtera): Pass the +define's to the preprocessor, and only
  // generate variants with theses defines fixed.

  const int limit_variants = absl::GetFlag(FLAGS_limit_variants);

  if (files.empty()) {
    return absl::InvalidArgumentError("ERROR: Missing file argument.");
  }
  if (files.size() > 1) {
    return absl::InvalidArgumentError(
        "ERROR: generate-variants only works on one file.");
  }
  const auto& source_file = files[0];
  std::string source_contents;
  if (auto status = verible::file::GetContents(source_file, &source_contents);
      !status.ok()) {
    message_stream << source_file << status;
    return status;
  }

  // Lexing the input SV source code.
  verilog::VerilogLexer lexer(source_contents);
  verible::TokenSequence lexed_sequence;
  for (lexer.DoNextToken(); !lexer.GetLastToken().isEOF();
       lexer.DoNextToken()) {
    // For now we will store the syntax tree tokens only, ignoring all the
    // white-space characters. however that should be stored to output the
    // source code just like it was.
    if (verilog::VerilogLexer::KeepSyntaxTreeTokens(lexer.GetLastToken())) {
      lexed_sequence.push_back(lexer.GetLastToken());
    }
  }

  // Control flow tree constructing.
  verilog::FlowTree control_flow_tree(lexed_sequence);
  int counter = 0;
  auto status = control_flow_tree.GenerateVariants(
      [limit_variants, &outs, &message_stream,
       &counter](const verilog::FlowTree::Variant& variant) {
        if (counter == limit_variants) return false;
        counter++;
        message_stream << "Variant number " << counter << ":\n";
        for (auto token : variant.sequence) outs << token << '\n';
        // TODO(karimtera): Consider creating an output file per vairant,
        // Such that the files naming reflects which defines are
        // defined/undefined.
        return true;
      });
  if (!status.ok()) {
    return status;
  }

  return absl::OkStatus();
}

static const std::pair<absl::string_view, SubcommandEntry> kCommands[] = {
    {"strip-comments",
     {&StripComments,
      R"(strip-comments file [replacement-char]
Inputs:
  'file' is a Verilog or SystemVerilog source file.
  Use '-' to read from stdin.
  'replacement-char' is a character to replace comments with.
  If not given, or given as a single space character, the comment contents and
  delimiters are replaced with spaces.
  If an empty string, the comment contents and delimiters are deleted. Newlines
  are not deleted.
  If a single character, the comment contents are replaced with the character.
Output: (stdout)
  Contents of original file with // and /**/ comments removed.
)"}},
    {"multiple-compilation-unit",
     {&MultipleCU,
      R"(multiple-compilation-unit file [more_files]
Inputs:
  'file' is a Verilog or SystemVerilog source file.
   There can be multiple SystemVerilog source files.
   Each one of them will be prepropcessed separatly which means that declarations
   scoopes will end by the end of each file, and won't be seen from other files.
Output: (stdout)
  The preprocessed files content (same contents with directives interpreted).
)"}},
    {"generate-variants",
     {&GenerateVariants,
      R"(generate-variants file [-limit_variants number]
Inputs:
  'file' is a Verilog or SystemVerilog source file.
  '-limit_variants' flag limits variants to 'number' (20 by default).
Output: (stdout)
   Generates every possible variant considering the conditional directives.
)"}},
    // TODO(karimtera): We can add another argument to `generate-variants`,
    // Which allows us to set some defines, as if we are only interested
    // in the variants in which these defines are set.

    // TODO(karimtera): Another candidate subcommand is `list-defines`,
    // Which would be the output of `GetUsedMacros()`.
};

int main(int argc, char* argv[]) {
  // Create a registry of subcommands (locally, rather than as a static global).
  verible::SubcommandRegistry commands;
  for (const auto& entry : kCommands) {
    const auto status = commands.RegisterCommand(entry.first, entry.second);
    if (!status.ok()) {
      std::cerr << status.message() << std::endl;
      return 2;  // fatal error
    }
  }

  const std::string usage = absl::StrCat("usage: ", argv[0],
                                         " command args...\n"
                                         "available commands:\n",
                                         commands.ListCommands());

  // Process invocation args.
  const auto args = verible::InitCommandLine(usage, &argc, &argv);
  if (args.size() == 1) {
    std::cerr << absl::ProgramUsageMessage() << std::endl;
    return 1;
  }
  // args[0] is the program name
  // args[1] is the subcommand
  // subcommand args start at [2]
  const SubcommandArgsRange command_args(args.cbegin() + 2, args.cend());

  const auto& sub = commands.GetSubcommandEntry(args[1]);
  // Run the subcommand.
  const auto status = sub.main(command_args, std::cin, std::cout, std::cerr);
  if (!status.ok()) {
    std::cerr << status.message() << std::endl;
    return 1;
  }
  return 0;
}
