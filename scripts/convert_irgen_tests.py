#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
Converts IRGenerator_test.cpp test cases into .endo test files.

Parses each TEST_CASE block, identifies the test pattern, extracts source code
and expected output, and generates an .endo file with appropriate directives.
"""

import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional


@dataclass
class TestAction:
    """A single test assertion extracted from a TEST_CASE body."""
    kind: str  # 'output', 'ir_only', 'ir_error', 'executes', 'exit_code', 'session', 'structured', 'structured_nonempty'
    source: str
    expected_output: Optional[str] = None
    expected_error: Optional[str] = None
    expected_exit_code: Optional[int] = None
    session_prompts: list[str] = field(default_factory=list)
    mock_env: list[tuple[str, str]] = field(default_factory=list)
    mock_which: list[tuple[str, str]] = field(default_factory=list)
    expected_env: list[tuple[str, str]] = field(default_factory=list)
    comment: Optional[str] = None


@dataclass
class TestCase:
    """A parsed TEST_CASE block from the C++ file."""
    name: str
    line_number: int
    body: str
    actions: list[TestAction] = field(default_factory=list)


# ============================================================================
# Directory mapping from test name prefix to output directory
# ============================================================================

def get_directory(test_name: str) -> str:
    """Map a test case name to its output directory."""
    # Strip common prefixes
    name = test_name
    for prefix in ['IRGenerator.', 'FSharp.', 'Shell.', 'StructuredPipeline.', 'DataSource.', 'BareExpr.']:
        name = name.removeprefix(prefix)

    # Specific prefix-to-directory mappings (ordered from most specific to least)
    mappings = [
        # Session tests
        ('session_', 'session'),
        # Let-in expressions
        ('let_in_', 'let-in'),
        # Export tests
        ('let_export_', 'export'),
        # Let bindings (basics)
        ('let_', 'basics'),
        ('list_multiline_join', 'basics'),
        # Identifiers
        ('identifier_', 'basics'),
        # Arithmetic and comparisons
        ('binary_', 'arithmetic'),
        ('unary_', 'arithmetic'),
        ('paren_', 'arithmetic'),
        ('combined_', 'arithmetic'),
        ('exec_arith_', 'arithmetic'),
        ('exec_comparison_', 'arithmetic'),
        ('exec_logical_', 'arithmetic'),
        ('logical_', 'arithmetic'),
        ('float_', 'arithmetic'),
        ('hex_', 'arithmetic'),
        ('octal_', 'arithmetic'),
        ('binary_literal', 'arithmetic'),
        ('binary_arithmetic', 'arithmetic'),
        ('scientific_', 'arithmetic'),
        # Functions
        ('function_', 'functions'),
        ('closure_', 'functions'),
        ('exec_closure_', 'functions'),
        ('exec_partial_', 'functions'),
        ('exec_multiple_function_calls', 'functions'),
        ('exec_function_call_', 'functions'),
        ('hof_', 'functions'),
        ('compose_', 'functions'),
        ('bare_call_', 'functions'),
        ('variadic.', 'functions'),
        ('ArityEnforcement.', 'functions'),
        # Recursion
        ('exec_rec_', 'recursion'),
        ('mutual_rec_', 'recursion'),
        ('nested_rec_', 'recursion'),
        ('nested_non_recursive_', 'recursion'),
        ('deeply_nested_', 'recursion'),
        ('non_tail_rec_', 'recursion'),
        # Lambdas and placeholders
        ('lambda_', 'lambdas'),
        ('placeholder_', 'lambdas'),
        # Control flow
        ('if_', 'control-flow'),
        ('mutable_assignment_', 'control-flow'),
        ('immutable_assignment_', 'errors'),
        ('exec_multiline_', 'control-flow'),
        ('exec_mixed_', 'control-flow'),
        ('block_scope_', 'control-flow'),
        ('stmt_', 'control-flow'),
        ('bare_range_', 'control-flow'),
        ('for_in_', 'control-flow'),
        # Match expressions
        ('match_literal_', 'match'),
        ('match_wildcard', 'match'),
        ('match_variable_', 'match'),
        ('match_with_', 'match'),
        ('match_guard_', 'match'),
        ('match_in_', 'match'),
        ('match_bool_', 'match'),
        ('exec_match_option_', 'match'),
        ('exec_match_result_', 'match'),
        ('exec_match_constructor_', 'match'),
        ('exec_match_guard_', 'match'),
        ('exec_multiline_match', 'control-flow'),
        ('match_option_', 'match'),
        ('match_result_', 'match'),
        # Patterns
        ('exec_or_pattern_', 'patterns'),
        ('exec_as_pattern_', 'patterns'),
        ('match_bare_tuple_', 'patterns'),
        ('tuple_destructure_', 'patterns'),
        ('match_empty_list', 'lists'),
        ('match_cons_', 'lists'),
        ('match_nonempty_', 'lists'),
        ('match_fixed_length_', 'lists'),
        ('match_recursive_', 'lists'),
        # Types
        ('option_some', 'types'),
        ('option_none', 'types'),
        ('option_some_with_', 'types'),
        ('option_match', 'types'),
        ('option_some_bool', 'types'),
        ('option_nested', 'types'),
        ('option_chain', 'types'),
        ('option_scope_', 'types'),
        ('option_default_', 'types'),
        ('option_map_', 'types'),
        ('option_bind_', 'types'),
        ('option_defaultValue_', 'types'),
        ('option_chained_', 'types'),
        ('option_in_binary_', 'types'),
        ('option_unwrapped_', 'types'),
        ('optional_chain_', 'types'),
        ('option.dot_', 'types'),
        ('result_ok', 'types'),
        ('result_error', 'types'),
        ('result_ok_with_', 'types'),
        ('result_match', 'types'),
        ('result_error_value', 'types'),
        ('result_chain', 'types'),
        ('result_nested_', 'types'),
        ('result_scope_', 'types'),
        ('nested_scope_', 'types'),
        ('result_in_binary_', 'types'),
        ('result_unwrapped_', 'types'),
        ('result.dot_', 'types'),
        ('exec_option_', 'types'),
        ('exec_result_', 'types'),
        ('exec_nested_option', 'types'),
        ('exec_option_in_result', 'types'),
        ('tuple_fst', 'types'),
        ('tuple_snd', 'types'),
        ('tuple_pattern_match', 'types'),
        ('tuple_3_', 'types'),
        ('tuple_swap', 'types'),
        ('tuple_sum_pair', 'types'),
        ('tuple_mixed_types', 'types'),
        ('tuple_numeric_snd', 'types'),
        ('tuple_snd_via_', 'types'),
        ('tuple_fst_direct', 'types'),
        ('tuple_snd_direct', 'types'),
        ('tuple.dot_', 'types'),
        ('tuple_env_', 'types'),
        ('tuple_if_', 'types'),
        ('TypeAnnotation.', 'types'),
        ('type_tag.', 'types'),
        # Try/catch
        ('try_with_', 'try-catch'),
        ('try_expr_', 'try-catch'),
        ('exec_try_', 'try-catch'),
        ('exec_trywith_', 'try-catch'),
        ('exec_tryfinally_', 'try-catch'),
        ('try_toplevel_', 'try-catch'),
        # Lists
        ('list_', 'lists'),
        ('list.', 'lists'),
        ('cons_', 'lists'),
        ('char_range_', 'lists'),
        # Strings
        ('string_', 'strings'),
        ('fstring_', 'strings'),
        ('string.dot_', 'strings'),
        ('string_concat_', 'strings'),
        # Records
        ('record_', 'records'),
        ('union_', 'records'),
        ('method.', 'records'),
        # Builtins
        ('exec_print_', 'builtins'),
        ('exec_println_', 'builtins'),
        ('print_bool_', 'builtins'),
        ('builtin_', 'builtins'),
        ('env_', 'builtins'),
        ('which_', 'builtins'),
        ('unit_', 'builtins'),
        ('formatMode.', 'builtins'),
        ('formatDateTime.', 'builtins'),
        ('formatNumber.', 'builtins'),
        ('isReadable.', 'builtins'),
        ('isWritable.', 'builtins'),
        ('isExecutable.', 'builtins'),
        ('fetch.', 'builtins'),
        ('rand_', 'builtins'),
        ('toBool_', 'builtins'),
        ('property.', 'builtins'),
        ('shell_is_interactive.', 'builtins'),
        # Shell
        ('compound_param_', 'shell'),
        ('command_substitution_', 'shell'),
        # Structured
        ('ps.', 'structured'),
        ('ls.', 'structured'),
        ('structured.', 'structured'),
        ('docker_ps.', 'structured'),
        ('docker_images.', 'structured'),
        ('git_log.', 'structured'),
        ('git_status.', 'structured'),
        ('no_definition_', 'structured'),
        ('pipeline_to_', 'structured'),
        ('each_println_records', 'structured'),
        ('open_json_', 'structured'),
        ('open_csv_', 'structured'),
        ('from_json_', 'structured'),
        ('from_csv_', 'structured'),
        ('unknown_named_type_', 'structured'),
        ('pipe_chain_from_', 'structured'),
        # Exec command
        ('exec_single_command', 'builtins'),
        ('exec_multiple_args', 'builtins'),
        ('exec_variable_', 'builtins'),
        ('exec_ir_', 'builtins'),
        ('exec_pipeline_ir_', 'builtins'),
        ('exec_three_stage_', 'builtins'),
        ('exec_in_match_', 'builtins'),
        ('exec_with_which', 'builtins'),
        ('exec_with_tuple_', 'builtins'),
        # Bare expressions
        ('number', 'basics'),
        ('arithmetic', 'basics'),
        ('list', 'basics'),
        ('list_parenthesized', 'basics'),
        ('option_some', 'basics'),
        ('option_none', 'basics'),
        ('result_ok', 'basics'),
        ('tuple', 'basics'),
        ('bool_true', 'basics'),
        ('bool_false', 'basics'),
        ('lambda_apply', 'basics'),
        ('nested_multiply', 'basics'),
        ('result_error', 'basics'),
        ('negative_number', 'basics'),
        ('float_literal', 'basics'),
        # Regression / misc
        ('parenthesized_', 'regression'),
        ('exec_preserves_exit_code', 'builtins'),
        ('comment_', 'basics'),
        ('exec_simple_arithmetic', 'arithmetic'),
        # Errors
        ('list_if_type_mismatch_', 'errors'),
        ('list_if_same_type_', 'errors'),
        ('list_function_arity_', 'errors'),
        ('list_recursive_non_tail_', 'errors'),
        ('list_heterogeneous_', 'errors'),
        ('list_homogeneous_', 'lists'),
    ]

    # Check BareExpr prefix first
    if test_name.startswith('IRGenerator.BareExpr.'):
        bare_name = test_name.removeprefix('IRGenerator.BareExpr.')
        return 'basics'

    # Check StructuredPipeline prefix
    if test_name.startswith('IRGenerator.StructuredPipeline.'):
        return 'structured'

    # Check DataSource prefix
    if test_name.startswith('IRGenerator.DataSource.'):
        return 'structured'

    # Check Shell prefix
    if test_name.startswith('IRGenerator.Shell.'):
        return 'shell'

    for prefix, directory in mappings:
        if name.startswith(prefix):
            return directory

    return 'regression'


def get_filename(test_name: str) -> str:
    """Convert a test case name to a file name."""
    # Strip all prefixes to get the core name
    name = test_name
    for prefix in ['IRGenerator.', 'FSharp.', 'Shell.', 'StructuredPipeline.', 'DataSource.', 'BareExpr.']:
        name = name.removeprefix(prefix)
    # Convert dots to underscores
    name = name.replace('.', '_')
    # Ensure valid filename chars
    name = re.sub(r'[^a-zA-Z0-9_]', '_', name)
    return name


# ============================================================================
# C++ string literal parsing
# ============================================================================

def parse_raw_string(s: str, pos: int) -> tuple[str, int]:
    """Parse a R"delimiter(...)delimiter" raw string starting at 'R'."""
    assert s[pos] == 'R' and s[pos + 1] == '"'
    pos += 2  # skip R"
    # Find delimiter
    delim_end = s.index('(', pos)
    delimiter = s[pos:delim_end]
    pos = delim_end + 1  # skip (
    # Find closing )delimiter"
    end_marker = ')' + delimiter + '"'
    end_pos = s.index(end_marker, pos)
    content = s[pos:end_pos]
    return content, end_pos + len(end_marker)


def parse_regular_string(s: str, pos: int) -> tuple[str, int]:
    """Parse a regular "..." string literal starting at the opening quote."""
    assert s[pos] == '"'
    pos += 1
    result = []
    while pos < len(s) and s[pos] != '"':
        if s[pos] == '\\':
            pos += 1
            if pos < len(s):
                c = s[pos]
                if c == 'n':
                    result.append('\n')
                elif c == 't':
                    result.append('\t')
                elif c == 'r':
                    result.append('\r')
                elif c == '"':
                    result.append('"')
                elif c == '\\':
                    result.append('\\')
                else:
                    result.append('\\')
                    result.append(c)
                pos += 1
        else:
            result.append(s[pos])
            pos += 1
    if pos < len(s):
        pos += 1  # skip closing quote
    return ''.join(result), pos


def extract_string_literal(s: str, pos: int) -> tuple[str, int]:
    """Extract a (possibly concatenated) string literal starting at pos."""
    parts = []
    while pos < len(s):
        # Skip whitespace
        while pos < len(s) and s[pos] in ' \t\n\r':
            pos += 1
        if pos >= len(s):
            break

        if s[pos] == 'R' and pos + 1 < len(s) and s[pos + 1] == '"':
            content, pos = parse_raw_string(s, pos)
            parts.append(content)
        elif s[pos] == '"':
            content, pos = parse_regular_string(s, pos)
            parts.append(content)
        else:
            break

        # Check for string concatenation (implicit via adjacency)
        # Skip whitespace to see if there's another string
        saved_pos = pos
        while pos < len(s) and s[pos] in ' \t\n\r':
            pos += 1
        if pos < len(s) and (s[pos] == '"' or (s[pos] == 'R' and pos + 1 < len(s) and s[pos + 1] == '"')):
            continue
        else:
            pos = saved_pos
            break

    return ''.join(parts), pos


# ============================================================================
# Test body parsing
# ============================================================================

def find_string_arg(body: str, func_call: str) -> Optional[str]:
    """Find the string argument to a function call like executeSourceAndGetOutput(...).
    Note: func_call should include the opening paren, e.g. 'executeSourceAndGetOutput('."""
    idx = body.find(func_call)
    if idx == -1:
        return None
    # func_call includes '(' already, so pos is right after it
    pos = idx + len(func_call)
    # Skip whitespace
    while pos < len(body) and body[pos] in ' \t\n\r':
        pos += 1
    if pos >= len(body):
        return None
    result, _ = extract_string_literal(body, pos)
    return result


def find_two_string_args(body: str, func_call: str) -> Optional[tuple[str, str]]:
    """Find two string arguments to a function call like generatesIRWithError(src, msg).
    Note: func_call should include the opening paren."""
    idx = body.find(func_call)
    if idx == -1:
        return None
    # func_call includes '(' already
    pos = idx + len(func_call)
    while pos < len(body) and body[pos] in ' \t\n\r':
        pos += 1
    src, pos = extract_string_literal(body, pos)
    # Skip to comma
    while pos < len(body) and body[pos] in ' \t\n\r':
        pos += 1
    if pos < len(body) and body[pos] == ',':
        pos += 1
    while pos < len(body) and body[pos] in ' \t\n\r':
        pos += 1
    msg, pos = extract_string_literal(body, pos)
    return src, msg


def find_exit_code_arg(body: str) -> Optional[tuple[str, int]]:
    """Find args for executesWithExitCode(src, code)."""
    m = re.search(r'executesWithExitCode\s*\(', body)
    if not m:
        return None
    pos = m.end()
    while pos < len(body) and body[pos] in ' \t\n\r':
        pos += 1
    src, pos = extract_string_literal(body, pos)
    # Skip to comma
    while pos < len(body) and body[pos] in ' \t\n\r':
        pos += 1
    if pos < len(body) and body[pos] == ',':
        pos += 1
    while pos < len(body) and body[pos] in ' \t\n\r':
        pos += 1
    # Parse integer
    code_str = ''
    while pos < len(body) and (body[pos].isdigit() or body[pos] == '-'):
        code_str += body[pos]
        pos += 1
    if not code_str:
        return None
    return src, int(code_str)


def find_session_args(body: str) -> Optional[tuple[list[str], str]]:
    """Find args for sessionProducesOutput({ "p1", "p2" }, "expected")."""
    idx = body.find('sessionProducesOutput(')
    if idx == -1:
        return None
    pos = idx + len('sessionProducesOutput(')
    while pos < len(body) and body[pos] in ' \t\n\r':
        pos += 1
    # Expect opening brace for initializer list
    if pos < len(body) and body[pos] == '{':
        pos += 1
    else:
        return None

    prompts = []
    while pos < len(body):
        while pos < len(body) and body[pos] in ' \t\n\r,':
            pos += 1
        if pos < len(body) and body[pos] == '}':
            pos += 1
            break
        if pos >= len(body):
            break
        prompt, pos = extract_string_literal(body, pos)
        prompts.append(prompt)

    # Skip to comma and expected output
    while pos < len(body) and body[pos] in ' \t\n\r':
        pos += 1
    if pos < len(body) and body[pos] == ',':
        pos += 1
    while pos < len(body) and body[pos] in ' \t\n\r':
        pos += 1
    expected, pos = extract_string_literal(body, pos)
    return prompts, expected


def find_structured_output_args(body: str) -> Optional[tuple[str, str]]:
    """Find args for structuredExecutesWithOutput(src, expected)."""
    idx = body.find('structuredExecutesWithOutput(')
    if idx == -1:
        return None
    pos = idx + len('structuredExecutesWithOutput(')
    while pos < len(body) and body[pos] in ' \t\n\r':
        pos += 1
    src, pos = extract_string_literal(body, pos)
    while pos < len(body) and body[pos] in ' \t\n\r,':
        pos += 1
    expected, pos = extract_string_literal(body, pos)
    return src, expected


def extract_mock_env(body: str) -> list[tuple[str, str]]:
    """Extract all setMockEnvVar("key", "value") calls."""
    result = []
    for m in re.finditer(r'setMockEnvVar\s*\(\s*', body):
        pos = m.end()
        key, pos = extract_string_literal(body, pos)
        while pos < len(body) and body[pos] in ' \t\n\r,':
            pos += 1
        value, pos = extract_string_literal(body, pos)
        result.append((key, value))
    return result


def extract_mock_which(body: str) -> list[tuple[str, str]]:
    """Extract all setMockWhichPath("prog", "/path") calls."""
    result = []
    for m in re.finditer(r'setMockWhichPath\s*\(\s*', body):
        pos = m.end()
        prog, pos = extract_string_literal(body, pos)
        while pos < len(body) and body[pos] in ' \t\n\r,':
            pos += 1
        path, pos = extract_string_literal(body, pos)
        result.append((prog, path))
    return result


def extract_expected_env(body: str) -> list[tuple[str, str]]:
    """Extract all testRuntime.env().at("key") == "value" checks."""
    result = []
    for m in re.finditer(r'(?:testRuntime|rt)\.env\(\)\.at\s*\(\s*', body):
        pos = m.end()
        key, pos = extract_string_literal(body, pos)
        # Find == and value
        eq_match = re.search(r'==\s*', body[pos:])
        if eq_match:
            vpos = pos + eq_match.end()
            value, _ = extract_string_literal(body, vpos)
            result.append((key, value))
    return result


def extract_comment(body: str) -> Optional[str]:
    """Extract the first // comment from the body as a description."""
    m = re.search(r'//\s*(.+?)$', body, re.MULTILINE)
    if m:
        return m.group(1).strip()
    return None


def parse_test_body(test: TestCase) -> list[TestAction]:
    """Parse a TEST_CASE body into a list of TestActions."""
    body = test.body
    actions = []
    mock_env = extract_mock_env(body)
    mock_which = extract_mock_which(body)
    expected_env = extract_expected_env(body)
    comment = extract_comment(body)

    # Check for executeSourceWithStructuredState (bare structured test with non-empty check)
    struct_state_match = re.search(r'executeSourceWithStructuredState\s*\(', body)
    if struct_state_match and '!result->output.empty()' in body:
        pos = struct_state_match.end()
        while pos < len(body) and body[pos] in ' \t\n\r':
            pos += 1
        src, _ = extract_string_literal(body, pos)
        if src:
            actions.append(TestAction(
                kind='structured_nonempty',
                source=src,
                comment=comment,
            ))
            return actions

    # Check for structuredExecutesWithOutput
    structured_args = find_structured_output_args(body)
    if structured_args:
        src, expected = structured_args
        actions.append(TestAction(
            kind='structured',
            source=src,
            expected_output=expected,
            comment=comment,
        ))
        return actions

    # Check for sessionProducesOutput
    session_args = find_session_args(body)
    if session_args:
        prompts, expected = session_args
        actions.append(TestAction(
            kind='session',
            source='',
            session_prompts=prompts,
            expected_output=expected,
            comment=comment,
        ))
        return actions

    # Check for executesWithExitCode
    exit_code_args = find_exit_code_arg(body)
    if exit_code_args:
        src, code = exit_code_args
        actions.append(TestAction(
            kind='exit_code',
            source=src,
            expected_exit_code=code,
            comment=comment,
            mock_env=mock_env,
            mock_which=mock_which,
        ))
        return actions

    # Handle auto [const] source/src = "..." pattern (intermediate variable for source)
    auto_src_match = re.search(r'auto\s+(?:const\s+)?(?:src|source)\s*=\s*', body)
    if auto_src_match:
        pos = auto_src_match.end()
        src, _ = extract_string_literal(body, pos)
        # Find CHECK(actual == "...") or CHECK(executeSourceAndGetOutput(source) == "...")
        actual_check = re.search(r'CHECK\s*\(\s*actual\s*==\s*', body)
        if actual_check:
            eq_pos = actual_check.end()
            expected, _ = extract_string_literal(body, eq_pos)
            actions.append(TestAction(
                kind='output',
                source=src,
                expected_output=expected,
                comment=comment,
                mock_env=mock_env,
                mock_which=mock_which,
            ))
            return actions
        # Find CHECK(executeSourceAndGetOutput(source) == "...")
        exec_check = re.search(r'CHECK\s*\(\s*executeSourceAndGetOutput\s*\(\s*source\s*\)\s*==\s*', body)
        if exec_check:
            eq_pos = exec_check.end()
            expected, _ = extract_string_literal(body, eq_pos)
            actions.append(TestAction(
                kind='output',
                source=src,
                expected_output=expected,
                comment=comment,
                mock_env=mock_env,
                mock_which=mock_which,
            ))
            return actions
        # Find REQUIRE_FALSE(result.has_value()) - expect execution failure
        if 'REQUIRE_FALSE' in body and 'result.has_value()' in body:
            # This is just checking that execution/parsing fails
            actions.append(TestAction(
                kind='ir_error',
                source=src,
                expected_error='',
                comment=comment or 'Should fail',
            ))
            return actions

    # Handle auto output = executeSourceAndGetOutput(...) with CAPTURE
    capture_output_match = re.search(r'auto\s+output\s*=\s*executeSourceAndGetOutput\s*\(', body)
    if capture_output_match:
        pos = capture_output_match.end()
        src, _ = extract_string_literal(body, pos)
        eq_match = re.search(r'CHECK\s*\(\s*output\s*==\s*', body)
        if eq_match:
            eq_pos = eq_match.end()
            expected, _ = extract_string_literal(body, eq_pos)
            actions.append(TestAction(
                kind='output',
                source=src,
                expected_output=expected,
                comment=comment,
                mock_env=mock_env,
                mock_which=mock_which,
            ))
            return actions

    # Handle auto const output = executeSourceAndGetOutput(...) pattern
    auto_output_match = re.search(r'auto\s+(?:const\s+)?output\s*=\s*executeSourceAndGetOutput\s*\(', body)
    if auto_output_match:
        pos = auto_output_match.end()
        src, _ = extract_string_literal(body, pos)
        # Find CHECK(output == "...")
        eq_match = re.search(r'CHECK\s*\(\s*output\s*==\s*', body)
        if eq_match:
            eq_pos = eq_match.end()
            expected, _ = extract_string_literal(body, eq_pos)
            actions.append(TestAction(
                kind='output',
                source=src,
                expected_output=expected,
                comment=comment,
                mock_env=mock_env,
                mock_which=mock_which,
            ))
            return actions
        # Check for partial match patterns (like formatNumber locale tests)
        if 'output.find(' in body:
            # Complex string matching - skip (would need manual conversion)
            actions.append(TestAction(kind='output', source=src, comment=comment))
            return actions

    # Handle auto result = executeSource(...) pattern
    auto_exec_match = re.search(r'auto\s+result\s*=\s*executeSource\s*\(', body)
    if auto_exec_match and 'executeSourceWithStructuredState' not in body:
        pos = auto_exec_match.end()
        src, _ = extract_string_literal(body, pos)
        # Check for REQUIRE_FALSE(result.has_value()) - expect failure
        if 'REQUIRE_FALSE' in body:
            actions.append(TestAction(
                kind='ir_error',
                source=src,
                expected_error='',
                comment=comment or 'Should fail',
            ))
            return actions
        # Check for result->output ==
        out_match = re.search(r'result->output\s*==\s*', body)
        if out_match:
            eq_pos = out_match.end()
            expected, _ = extract_string_literal(body, eq_pos)
            actions.append(TestAction(
                kind='output',
                source=src,
                expected_output=expected,
                comment=comment,
                mock_env=mock_env,
                mock_which=mock_which,
            ))
            return actions
        # Just executesSuccessfully equivalent
        actions.append(TestAction(kind='executes', source=src, comment=comment))
        return actions

    # Count different CHECK/REQUIRE patterns to handle multi-check tests
    # Find all executeSourceAndGetOutput calls
    output_matches = list(re.finditer(r'(?:CHECK|REQUIRE)\s*\(\s*executeSourceAndGetOutput\s*\(', body))
    ir_success_matches = list(re.finditer(r'(?:CHECK|REQUIRE)\s*\(\s*generatesIRSuccessfully\s*\(', body))
    ir_error_matches = list(re.finditer(r'(?:CHECK|REQUIRE)\s*\(\s*generatesIRWithError\s*\(', body))
    executes_matches = list(re.finditer(r'(?:CHECK|REQUIRE)\s*\(\s*executesSuccessfully\s*\(', body))
    # Also check for negated IR success: CHECK(!generatesIRSuccessfully(...))
    ir_fail_matches = list(re.finditer(r'(?:CHECK|REQUIRE)\s*\(\s*!generatesIRSuccessfully\s*\(', body))

    # Handle multi-CHECK tests by extracting each CHECK separately
    if len(output_matches) > 1 or (output_matches and (ir_success_matches or expected_env)):
        for m in output_matches:
            pos = m.end()
            src, pos = extract_string_literal(body, pos)
            # Find == and expected output
            while pos < len(body) and body[pos] in ' \t\n\r)':
                pos += 1
            eq_idx = body.find('==', pos - 1)
            if eq_idx != -1:
                eq_pos = eq_idx + 2
                while eq_pos < len(body) and body[eq_pos] in ' \t\n\r':
                    eq_pos += 1
                expected, _ = extract_string_literal(body, eq_pos)
                actions.append(TestAction(
                    kind='output',
                    source=src,
                    expected_output=expected,
                    comment=comment,
                    mock_env=mock_env,
                    mock_which=mock_which,
                    expected_env=expected_env,
                ))
        for m in ir_success_matches:
            pos = m.end()
            src, _ = extract_string_literal(body, pos)
            actions.append(TestAction(
                kind='ir_only',
                source=src,
                comment=comment,
            ))
        return actions

    # Single output check
    if output_matches:
        m = output_matches[0]
        pos = m.end()
        src, pos = extract_string_literal(body, pos)
        # Find == "expected"
        remaining = body[pos:]
        eq_match = re.search(r'\)\s*==\s*', remaining)
        if eq_match:
            eq_pos = pos + eq_match.end()
            expected, _ = extract_string_literal(body, eq_pos)
            actions.append(TestAction(
                kind='output',
                source=src,
                expected_output=expected,
                comment=comment,
                mock_env=mock_env,
                mock_which=mock_which,
                expected_env=expected_env,
            ))
            return actions

    # IR generation success
    if ir_success_matches:
        for m in ir_success_matches:
            pos = m.end()
            src, _ = extract_string_literal(body, pos)
            actions.append(TestAction(
                kind='ir_only',
                source=src,
                comment=comment,
            ))
        return actions

    # IR error (generatesIRWithError)
    if ir_error_matches:
        for m in ir_error_matches:
            # Extract the two string args directly using position after the regex match
            pos = m.end()
            while pos < len(body) and body[pos] in ' \t\n\r':
                pos += 1
            src, pos = extract_string_literal(body, pos)
            # Skip to comma
            while pos < len(body) and body[pos] in ' \t\n\r':
                pos += 1
            if pos < len(body) and body[pos] == ',':
                pos += 1
            while pos < len(body) and body[pos] in ' \t\n\r':
                pos += 1
            msg, _ = extract_string_literal(body, pos)
            actions.append(TestAction(
                kind='ir_error',
                source=src,
                expected_error=msg,
                comment=comment,
            ))
        return actions

    # Negated IR success: CHECK(!generatesIRSuccessfully(...)) → expect IR failure
    # Use empty expected_error to indicate "any error" (wildcard)
    if ir_fail_matches:
        for m in ir_fail_matches:
            pos = m.end()
            while pos < len(body) and body[pos] in ' \t\n\r':
                pos += 1
            src, _ = extract_string_literal(body, pos)
            actions.append(TestAction(
                kind='ir_error',
                source=src,
                expected_error='',
                comment=comment or 'Should fail IR generation',
            ))
        return actions

    # executesSuccessfully
    if executes_matches:
        for m in executes_matches:
            pos = m.end()
            src, _ = extract_string_literal(body, pos)
            actions.append(TestAction(
                kind='executes',
                source=src,
                comment=comment,
            ))
        return actions

    return actions


# ============================================================================
# .endo file generation
# ============================================================================

def generate_endo_content(test_name: str, action: TestAction) -> str:
    """Generate the content of an .endo test file from a TestAction."""
    lines = ['# SPDX-License-Identifier: Apache-2.0', '#']

    # Description
    desc = action.comment or test_name_to_description(test_name)
    lines.append(f'# description: {desc}')

    # Mock directives
    for key, value in action.mock_env:
        lines.append(f'# mock-env: {key}={value}')
    for prog, path in action.mock_which:
        lines.append(f'# mock-which: {prog}={path}')

    # Mode
    if action.kind == 'ir_only':
        lines.append('# mode: ir-only')
    elif action.kind in ('structured', 'structured_nonempty'):
        lines.append('# mode: structured')

    # Expected output / error / exit code
    if action.kind == 'ir_error':
        if action.expected_error:
            lines.append(f'# expect-error: {action.expected_error}')
        else:
            lines.append('# expect-error:')
    elif action.kind == 'exit_code' and action.expected_exit_code is not None:
        lines.append(f'# expect-exit: {action.expected_exit_code}')
    elif action.kind == 'structured_nonempty':
        lines.append('# expect-nonempty')
    elif action.expected_output is not None:
        # With the separator model: lines are joined by \n (not terminated).
        # To express trailing \n, add empty # expect: at end.
        output = action.expected_output
        if output == '':
            pass  # No expect line needed for empty output
        else:
            # Split into segments by \n
            segments = output.split('\n')
            # Each segment becomes an # expect: line
            # Trailing empty string from split means the output ended with \n
            for seg in segments:
                lines.append(f'# expect: {seg}')

    # Expected env
    for key, value in action.expected_env:
        lines.append(f'# expect-env: {key}={value}')

    # Session separator
    if action.kind == 'session':
        lines.append('# session-separator: ---')

    lines.append('')  # Empty line before source

    # Source code
    if action.kind == 'session':
        for i, prompt in enumerate(action.session_prompts):
            if i > 0:
                lines.append('# ---')
            lines.append(prompt)
    else:
        lines.append(action.source)

    return '\n'.join(lines) + '\n'


def test_name_to_description(test_name: str) -> str:
    """Convert a test name to a human-readable description."""
    name = test_name
    for prefix in ['IRGenerator.', 'FSharp.', 'Shell.', 'StructuredPipeline.', 'DataSource.', 'BareExpr.']:
        name = name.removeprefix(prefix)
    # Convert underscores/dots to spaces, capitalize first word
    name = name.replace('_', ' ').replace('.', ' ')
    # Capitalize first letter
    if name:
        name = name[0].upper() + name[1:]
    return name


# ============================================================================
# C++ file parsing
# ============================================================================

def extract_test_cases(content: str) -> list[TestCase]:
    """Extract all TEST_CASE blocks from the C++ file."""
    tests = []
    i = 0
    lines = content.split('\n')

    while i < len(lines):
        line = lines[i]
        m = re.match(r'\s*TEST_CASE\s*\(\s*"([^"]+)"\s*\)', line)
        if m:
            test_name = m.group(1)
            line_number = i + 1

            # Find opening brace
            j = i
            while j < len(lines) and '{' not in lines[j]:
                j += 1

            # Count braces to find the end of the test body
            brace_count = 0
            body_lines = []
            start_j = j
            while j < len(lines):
                body_lines.append(lines[j])
                brace_count += lines[j].count('{') - lines[j].count('}')
                if brace_count <= 0 and j > start_j:
                    break
                j += 1

            body = '\n'.join(body_lines)
            tests.append(TestCase(name=test_name, line_number=line_number, body=body))
            i = j + 1
        else:
            i += 1

    return tests


# ============================================================================
# Main conversion logic
# ============================================================================

def find_existing_tests(test_dir: Path) -> set[str]:
    """Find all existing .endo test files and return their relative paths."""
    existing = set()
    if test_dir.exists():
        for f in test_dir.rglob('*.endo'):
            existing.add(str(f.relative_to(test_dir)))
    return existing


def main():
    project_root = Path(__file__).parent.parent
    cpp_file = project_root / 'src' / 'endo-language' / 'codegen' / 'IRGenerator_test.cpp'
    test_dir = project_root / 'tests'

    if not cpp_file.exists():
        print(f"Error: {cpp_file} not found", file=sys.stderr)
        sys.exit(1)

    print(f"Reading {cpp_file}...")
    content = cpp_file.read_text()

    print("Extracting TEST_CASE blocks...")
    test_cases = extract_test_cases(content)
    print(f"Found {len(test_cases)} TEST_CASE blocks")

    existing = find_existing_tests(test_dir)
    print(f"Found {len(existing)} existing .endo files")

    generated = 0
    skipped = 0
    failed = 0
    skipped_names = []

    for test in test_cases:
        # Skip commented-out tests
        if test.body.strip().startswith('//')  and '{' not in test.body:
            skipped += 1
            continue

        actions = parse_test_body(test)

        if not actions:
            failed += 1
            print(f"  SKIP (no actions): {test.name} (line {test.line_number})")
            continue

        for idx, action in enumerate(actions):
            directory = get_directory(test.name)
            filename = get_filename(test.name)
            if len(actions) > 1:
                filename = f"{filename}_{idx + 1}"

            relative_path = f"{directory}/{filename}.endo"

            # Skip if already exists
            if relative_path in existing:
                skipped += 1
                skipped_names.append(relative_path)
                continue

            # Generate content
            content = generate_endo_content(test.name, action)

            # Write file
            out_path = test_dir / relative_path
            out_path.parent.mkdir(parents=True, exist_ok=True)
            out_path.write_text(content)
            generated += 1

    print(f"\nResults:")
    print(f"  Generated: {generated}")
    print(f"  Skipped (existing): {skipped}")
    print(f"  Failed (unparseable): {failed}")

    if skipped_names:
        print(f"\nSkipped existing files:")
        for name in skipped_names[:10]:
            print(f"  {name}")
        if len(skipped_names) > 10:
            print(f"  ... and {len(skipped_names) - 10} more")


if __name__ == '__main__':
    main()
